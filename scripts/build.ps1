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
    Vérifications d'avant commit : formatage, clang-tidy, tests (alias -k).

.EXAMPLE
    scripts\build.ps1                       # debug + tests
.EXAMPLE
    scripts\build.ps1 release -r            # release, puis lance avenoir
.EXAMPLE
    scripts\build.ps1 -r -RunArgs cc.toml   # debug, tests, puis `avenoir cc.toml`
.EXAMPLE
    scripts\build.ps1 -k                    # tout vérifier avant de committer
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
    [Alias('k')][switch]$Check
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
if ($RunTests -or ($Check -and $Preset -ne 'release')) {
    Write-Step 'Tests'
    Invoke-Native ctest @('--preset', $Preset)
}

# ─── Vérifications d'avant commit ────────────────────────────────────────────
if ($Check) {
    Write-Step 'Vérification du formatage'
    Invoke-Native clang-format (@('--dry-run', '--Werror') + (Get-SourceFile '*.cpp', '*.hpp'))

    Write-Step 'Analyse statique (clang-tidy)'
    # Le code propre à Linux ne s'analyse pas ici (en-têtes POSIX absents).
    $sources = @(Get-SourceFile '*.cpp' | Where-Object { $_ -notmatch '/platform/linux\.cpp$' })
    Invoke-Native clang-tidy (@('-p', $BuildDir, '--quiet') + $sources)

    Write-Host ''
    Write-Host 'Tout est bon : prêt à committer.' -ForegroundColor Green
}

# ─── Lancement ───────────────────────────────────────────────────────────────
if ($Run) {
    Write-Step 'Lancement d''avenoir'
    & (Join-Path $BuildDir 'apps/cli/avenoir.exe') @RunArgs
    exit $LASTEXITCODE
}
