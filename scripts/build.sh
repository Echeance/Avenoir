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
#   -k, --check    vérifications d'avant commit : formatage, clang-tidy, tests
#   -h, --help     affiche cette aide
#
# Exemples :
#   scripts/build.sh                  # debug + tests
#   scripts/build.sh release -r       # release, puis lance avenoir
#   scripts/build.sh -r -- cc.toml    # debug, tests, puis `avenoir cc.toml`
#   scripts/build.sh asan -c          # build propre avec ASan + UBSan
#   scripts/build.sh -k               # tout vérifier avant de committer

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

preset=debug
clean=false
run=false
test=true
format=false
check=false
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
sources() { git ls-files '*.cpp' '*.hpp'; }

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
    step "Vérification du formatage"
    sources | xargs clang-format --dry-run --Werror

    step "Analyse statique (clang-tidy)"
    git ls-files '*.cpp' | xargs clang-tidy -p "$build_dir" --quiet

    printf '\n\033[1;32mTout est bon : prêt à committer.\033[0m\n'
fi

# ─── Lancement ───────────────────────────────────────────────────────────────
if $run; then
    step "Lancement d'avenoir"
    exec "$build_dir/apps/cli/avenoir" "${run_args[@]}"
fi
