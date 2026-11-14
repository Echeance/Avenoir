#!/usr/bin/env bash
# Compile Avenoir (et lance les tests) en une commande.
#
# Usage : scripts/build.sh [préréglage] [options] [-- arguments pour avenoir]
#
# Préréglages : debug (défaut), release, asan
#
# Options :
#   -c, --clean    supprime le dossier de build avant de compiler
#   -r, --run      lance `avenoir` après la compilation (arguments après `--`)
#   -n, --no-test  ne lance pas les tests
#   -f, --format   reformate le code (clang-format) avant de compiler
#   -k, --check    vérifications d'avant commit sur ce qui a changé depuis le
#                  dernier push (commité ou non) : formatage, clang-tidy (les
#                  .cpp modifiés et ceux qui incluent un en-tête modifié), tests
#   -K, --check-all les mêmes vérifications sur tout le code
#   -h, --help     affiche cette aide
#
# Exemples :
#   scripts/build.sh                  # debug + tests
#   scripts/build.sh release -r       # release, puis lance avenoir
#   scripts/build.sh -r -- cc.toml    # debug, tests, puis `avenoir cc.toml`
#   scripts/build.sh asan -c          # build propre avec ASan + UBSan
#   scripts/build.sh -k               # vérifier ses changements avant de committer
#   scripts/build.sh -K               # tout vérifier, quel que soit le changement

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

preset=debug
clean=false
run=false
test=true
format=false
check=false
check_all=false
run_args=()

usage() { sed -n '2,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"; }
step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[1;31mErreur : %s\033[0m\n' "$*" >&2; exit 1; }

while (($#)); do
    case "$1" in
        debug | release | asan) preset="$1" ;;
        -c | --clean) clean=true ;;
        -r | --run) run=true ;;
        -n | --no-test) test=false ;;
        -f | --format) format=true ;;
        -k | --check) check=true ;;
        -K | --check-all) check=true; check_all=true ;;
        -h | --help) usage; exit 0 ;;
        --) shift; run_args=("$@"); break ;;
        *) usage; fail "argument inconnu : $1" ;;
    esac
    shift
done

# Le préréglage release ne compile pas les tests.
[[ "$preset" == release ]] && test=false

# ─── Prérequis ───────────────────────────────────────────────────────────────
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
[[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] ||
    fail "vcpkg introuvable dans $VCPKG_ROOT (voir README, ou définir VCPKG_ROOT)"
command -v ninja >/dev/null || fail "Ninja n'est pas installé"

build_dir="build/$preset"
# Fichiers suivis et nouveaux fichiers non ignorés (pas encore ajoutés à git).
sources() { git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp'; }

# Fichiers changés depuis le dernier push (ou depuis le dernier commit sans
# branche suivie) : commités, modifiés ou nouveaux, sans les supprimés.
changed() {
    local base
    base="$(git merge-base HEAD '@{upstream}' 2>/dev/null)" || base=HEAD
    {
        git diff --name-only --diff-filter=d "$base" -- "$@"
        git ls-files --others --exclude-standard -- "$@"
    } | sort -u
}

# .cpp à analyser : ceux dont la compilation lit un fichier changé (le .cpp
# lui-même ou un en-tête, même inclus indirectement), d'après les dépendances
# relevées par Ninja à la compilation.
affected_sources() {
    local list
    list="$(mktemp)"
    changed '*.cpp' '*.hpp' >"$list"
    ninja -C "$build_dir" -t deps | awk -v root="$root/" -v list="$list" '
        BEGIN { while ((getline line < list) > 0) wanted[line] = 1 }
        /^[^ ]/ { source = ""; next }
        {
            path = $1
            if (index(path, root) == 1) path = substr(path, length(root) + 1)
            if (source == "") source = path
            if (path in wanted && source ~ /\.cpp$/) print source
        }' | sort -u
    rm -f "$list"
}

# ─── Formatage ───────────────────────────────────────────────────────────────
if $format; then
    step "Formatage (clang-format)"
    sources | xargs clang-format -i
fi

# ─── Configuration et compilation ────────────────────────────────────────────
if $clean; then
    step "Nettoyage de $build_dir"
    rm -rf "$build_dir"
fi

# La configuration n'est relancée que si nécessaire : Ninja la refait de
# lui-même quand un CMakeLists.txt change.
if [[ ! -f "$build_dir/build.ninja" ]]; then
    step "Configuration ($preset)"
    cmake --preset "$preset"
fi

step "Compilation ($preset)"
cmake --build --preset "$preset"

# ─── Tests ───────────────────────────────────────────────────────────────────
if $test || $check; then
    step "Tests"
    ctest --preset "$preset"
fi

# ─── Vérifications d'avant commit ────────────────────────────────────────────
if $check; then
    # Règles d'analyse ou de formatage changées : tout revérifier.
    if ! $check_all && [[ -n "$(changed .clang-tidy .clang-format)" ]]; then
        check_all=true
    fi
    if $check_all; then
        to_format="$(sources)"
        to_analyze="$(sources | grep '\.cpp$' || true)"
    else
        to_format="$(changed '*.cpp' '*.hpp')"
        to_analyze="$(affected_sources)"
    fi
    # Le code propre à Windows ne s'analyse pas ici (windows.h absent).
    to_analyze="$(grep -v '/platform/windows\.cpp$' <<<"$to_analyze" || true)"

    step "Vérification du formatage"
    if [[ -n "$to_format" ]]; then
        xargs clang-format --dry-run --Werror <<<"$to_format"
    else
        echo "Aucun fichier changé."
    fi

    step "Analyse statique (clang-tidy)"
    if [[ -n "$to_analyze" ]]; then
        jobs="$(nproc 2>/dev/null || echo 4)"
        echo "$(wc -l <<<"$to_analyze") fichier(s) à analyser ($jobs en parallèle)."
        # Un fichier par processus : xargs échoue si l'un d'eux signale un problème.
        xargs -P "$jobs" -n 1 clang-tidy -p "$build_dir" --quiet <<<"$to_analyze"
    else
        echo "Aucun fichier concerné."
    fi

    printf '\n\033[1;32mTout est bon : prêt à committer.\033[0m\n'
fi

# ─── Lancement ───────────────────────────────────────────────────────────────
if $run; then
    step "Lancement d'avenoir"
    exec "$build_dir/apps/cli/avenoir" "${run_args[@]}"
fi
