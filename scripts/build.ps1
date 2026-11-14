<#
.SYNOPSIS
    Compile Avenoir (et lance les tests) en une commande, sous Windows.

.DESCRIPTION
    Équivalent Windows de scripts/build.sh. Charge automatiquement
    l'environnement Visual Studio (MSVC, Ninja) s'il n'est pas déjà actif,
    puis enchaîne configuration, compilation et tests.

    Compatible Windows PowerShell 5.1 et PowerShell 7+.

.PARAMETER Preset
    debug (défaut) ou release. Le préréglage asan n'existe pas sous Windows.

.PARAMETER Clean
    Supprime le dossier de build avant de compiler (alias -c).

.PARAMETER Run
    Lance avenoir.exe après la compilation (alias -r).

.PARAMETER RunArgs
    Arguments passés à avenoir.exe avec -Run (ex. -RunArgs cc.toml).

.PARAMETER NoTest
    Ne lance pas les tests (alias -n).

.PARAMETER Format
    Reformate le code avec clang-format avant de compiler (alias -f).

.PARAMETER Check
    Vérifications d'avant commit sur ce qui a changé depuis le dernier push
    (commité ou non) : formatage, clang-tidy (les .cpp modifiés et ceux qui
    incluent un en-tête modifié), tests (alias -k).

.PARAMETER CheckAll
    Les mêmes vérifications sur tout le code (pas d'alias : -K vaudrait -k,
    PowerShell ne distinguant pas la casse).

.EXAMPLE
    scripts\build.ps1                       # debug + tests
.EXAMPLE
    scripts\build.ps1 release -r            # release, puis lance avenoir
.EXAMPLE
    scripts\build.ps1 -r -RunArgs cc.toml   # debug, tests, puis `avenoir cc.toml`
.EXAMPLE
    scripts\build.ps1 -k                    # vérifier ses changements avant de committer
.EXAMPLE
    scripts\build.ps1 -CheckAll             # tout vérifier, quel que soit le changement
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',

    [Alias('c')][switch]$Clean,
    [Alias('r')][switch]$Run,
    [string[]]$RunArgs = @(),
    [Alias('n')][switch]$NoTest,
    [Alias('f')][switch]$Format,
    [Alias('k')][switch]$Check,
    [switch]$CheckAll
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Blue
}

function Exit-WithError([string]$Message) {
    Write-Host "Erreur : $Message" -ForegroundColor Red
    exit 1
}

# Lance une commande externe et s'arrête si elle échoue (PowerShell 5.1 ne le
# fait pas tout seul pour les exécutables natifs).
function Invoke-Native {
    param([Parameter(Mandatory)][string]$Command, [string[]]$Arguments = @())
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        Exit-WithError "« $Command $($Arguments -join ' ') » a échoué (code $LASTEXITCODE)"
    }
}

function Get-SourceFile([string[]]$Patterns) {
    # Fichiers suivis et nouveaux fichiers non ignorés (pas encore ajoutés à git).
    $files = @(git ls-files --cached --others --exclude-standard @Patterns)
    if ($LASTEXITCODE -ne 0) { Exit-WithError 'git ls-files a échoué' }
    return $files
}

# Fichiers changés depuis le dernier push (ou depuis le dernier commit sans
# branche suivie) : commités, modifiés ou nouveaux, sans les supprimés.
function Get-ChangedFile([string[]]$Patterns) {
    # Sans branche suivie, git écrit une erreur : avec « Stop », Windows
    # PowerShell 5.1 s'arrêterait malgré la redirection.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $base = git merge-base HEAD '@{upstream}' 2>$null
    $ErrorActionPreference = $previous
    if ($LASTEXITCODE -ne 0 -or -not $base) { $base = 'HEAD' }
    $files = @(git diff --name-only --diff-filter=d $base -- @Patterns) +
        @(git ls-files --others --exclude-standard -- @Patterns)
    return @($files | Where-Object { $_ } | Sort-Object -Unique)
}

# .cpp à analyser : ceux qui ont changé, et ceux dont la compilation lit un
# en-tête changé (même inclus indirectement), d'après les dépendances relevées
# par Ninja. Le .cpp d'un objet se déduit de son nom
# (`<dossier>/CMakeFiles/<cible>.dir/<chemin>.cpp.obj` → `<dossier>/<chemin>.cpp`) :
# avec MSVC, Ninja ne le compte pas parmi les dépendances.
function Get-AffectedSource {
    $changed = @(Get-ChangedFile '*.cpp', '*.hpp')
    $wanted = @{}
    $found = @{}
    foreach ($file in $changed) {
        $wanted[$file] = $true
        if ($file -like '*.cpp') { $found[$file] = $true }
    }
    $prefix = ($Root -replace '\\', '/') + '/'
    $source = $null
    foreach ($line in (ninja -C $BuildDir -t deps)) {
        if ($line -notmatch '^\s') {
            $source = $null
            if ($line -match '^(.*)CMakeFiles/[^/]+\.dir/(.+\.cpp)\.(o|obj):') {
                $source = $Matches[1] + $Matches[2]
            }
            continue
        }
        if (-not $source) { continue }
        $path = $line.Trim() -replace '\\', '/'
        if ($path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            $path = $path.Substring($prefix.Length)
        }
        while ($path.StartsWith('../')) { $path = $path.Substring(3) }
        if ($wanted.ContainsKey($path)) { $found[$source] = $true }
    }
    if ($LASTEXITCODE -ne 0) { Exit-WithError 'ninja -t deps a échoué' }
    return @($found.Keys | Where-Object { Test-Path $_ } | Sort-Object)
}

# clang-tidy en parallèle (un lot de fichiers par processeur) : sortie de
# chaque lot affichée à la fin, échec si l'un d'eux signale un problème.
function Invoke-ClangTidy([string[]]$Files) {
    $count = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, $Files.Count))
    Write-Host "$($Files.Count) fichier(s) à analyser ($count en parallèle)."
    $jobs = for ($i = 0; $i -lt $count; $i++) {
        $chunk = @(for ($j = $i; $j -lt $Files.Count; $j += $count) { $Files[$j] })
        Start-Job -ArgumentList $Root, $BuildDir, $chunk -ScriptBlock {
            param($JobRoot, $JobBuildDir, [string[]]$Chunk)
            Set-Location $JobRoot
            $output = & clang-tidy -p $JobBuildDir --quiet @Chunk 2>&1 | Out-String
            [pscustomobject]@{ Output = $output; Code = $LASTEXITCODE }
        }
    }
    $failed = $false
    foreach ($result in @($jobs | Receive-Job -Wait -AutoRemoveJob)) {
        if ($result.Output.Trim()) { Write-Host $result.Output }
        if ($result.Code -ne 0) { $failed = $true }
    }
    if ($failed) { Exit-WithError 'clang-tidy a signalé des problèmes' }
}

# Le préréglage release ne compile pas les tests.
$RunTests = (-not $NoTest) -and ($Preset -ne 'release')

# ─── Prérequis ───────────────────────────────────────────────────────────────
# VCPKG_ROOT est fixé avant de charger Visual Studio, qui le remplacerait
# sinon par sa propre copie de vcpkg.
if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = Join-Path $HOME 'vcpkg' }
$VcpkgRoot = $env:VCPKG_ROOT
if (-not (Test-Path (Join-Path $VcpkgRoot 'scripts/buildsystems/vcpkg.cmake'))) {
    Exit-WithError "vcpkg introuvable dans $VcpkgRoot (voir README, ou définir VCPKG_ROOT)"
}

# Environnement MSVC : cl.exe et Ninja doivent être dans le PATH.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Step 'Chargement de l''environnement Visual Studio'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { Exit-WithError 'Visual Studio introuvable (vswhere.exe absent)' }
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { Exit-WithError 'Aucune installation de Visual Studio avec les outils C++ (MSVC)' }
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    Set-Location $Root
    $env:VCPKG_ROOT = $VcpkgRoot
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Exit-WithError 'Ninja introuvable (composant « Outils CMake C++ » de Visual Studio)'
}

$BuildDir = "build/$Preset"
# Dépendances liées statiquement, CRT dynamique : identique à la CI.
$Triplet = 'x64-windows-static-md'

# ─── Formatage ───────────────────────────────────────────────────────────────
if ($Format) {
    Write-Step 'Formatage (clang-format)'
    Invoke-Native clang-format (@('-i') + (Get-SourceFile '*.cpp', '*.hpp'))
}

# ─── Configuration et compilation ────────────────────────────────────────────
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Nettoyage de $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

# La configuration n'est relancée que si nécessaire : Ninja la refait de
# lui-même quand un CMakeLists.txt change.
if (-not (Test-Path (Join-Path $BuildDir 'build.ninja'))) {
    Write-Step "Configuration ($Preset)"
    Invoke-Native cmake @('--preset', $Preset, "-DVCPKG_TARGET_TRIPLET=$Triplet")
}

Write-Step "Compilation ($Preset)"
Invoke-Native cmake @('--build', '--preset', $Preset)

# ─── Tests ───────────────────────────────────────────────────────────────────
if ($RunTests -or (($Check -or $CheckAll) -and $Preset -ne 'release')) {
    Write-Step 'Tests'
    Invoke-Native ctest @('--preset', $Preset)
}

# ─── Vérifications d'avant commit ────────────────────────────────────────────
if ($Check -or $CheckAll) {
    # Règles d'analyse ou de formatage changées : tout revérifier.
    $all = $CheckAll -or (@(Get-ChangedFile '.clang-tidy', '.clang-format').Count -gt 0)
    if ($all) {
        $toFormat = @(Get-SourceFile '*.cpp', '*.hpp')
        $toAnalyze = @(Get-SourceFile '*.cpp')
    } else {
        $toFormat = @(Get-ChangedFile '*.cpp', '*.hpp')
        $toAnalyze = @(Get-AffectedSource)
    }
    # Le code propre à Linux ne s'analyse pas ici (en-têtes POSIX absents).
    $toAnalyze = @($toAnalyze | Where-Object { $_ -notmatch '/platform/linux\.cpp$' })

    Write-Step 'Vérification du formatage'
    if ($toFormat.Count -gt 0) {
        Invoke-Native clang-format (@('--dry-run', '--Werror') + $toFormat)
    } else {
        Write-Host 'Aucun fichier changé.'
    }

    Write-Step 'Analyse statique (clang-tidy)'
    if ($toAnalyze.Count -gt 0) {
        Invoke-ClangTidy $toAnalyze
    } else {
        Write-Host 'Aucun fichier concerné.'
    }

    Write-Host ''
    Write-Host 'Tout est bon : prêt à committer.' -ForegroundColor Green
}

# ─── Lancement ───────────────────────────────────────────────────────────────
if ($Run) {
    Write-Step 'Lancement d''avenoir'
    & (Join-Path $BuildDir 'apps/cli/avenoir.exe') @RunArgs
    exit $LASTEXITCODE
}
