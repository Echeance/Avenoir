#!/usr/bin/env bash
# Enregistre une vraie session de la CLI dans un pseudo-terminal, puis la
# dessine en image pour le README (docs/readme/cli-demo.png).
#
# Usage : docs/readme/capture.sh <config.toml>
#
# - La CLI doit être compilée (scripts/build.sh).
# - La session utilise un dossier de données temporaire : vos conversations
#   ne sont ni lues ni modifiées.
# - La deuxième requête interroge réellement le LLM configuré (un modèle
#   rapide comme « haiku » suffit).
# - Dépendances : script (util-linux), Python 3 et Pillow.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
config="${1:?Usage : $0 <config.toml>}"
config="$(cd "$(dirname "$config")" && pwd)/$(basename "$config")"
avenoir="$root/build/debug/apps/cli/avenoir"
[[ -x "$avenoir" ]] || { echo "Compilez d'abord : scripts/build.sh" >&2; exit 1; }

rows=42
columns=120
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Saisies envoyées à la CLI, au rythme d'une vraie session.
type_session() {
    sleep 1.5
    printf 'quelle heure est-il ?\r'
    sleep 2.5
    printf 'Décris mon ordinateur en 3 puces courtes, avec le nom de la distribution en gras.\r'
    sleep 20
    printf 'Ouvre la documentation de std::expected'
    sleep 3
}

echo "Enregistrement de la session (environ 30 s)…"
type_session | COLORTERM=truecolor XDG_DATA_HOME="$work/data" \
    timeout -s KILL 28 script -qfec "stty rows $rows cols $columns; '$avenoir' '$config'" \
    /dev/null >"$work/session.txt" 2>&1 || true

python3 "$root/docs/readme/terminal_render.py" "$work/session.txt" "$rows" "$columns" \
    "$root/docs/readme/cli-demo.png"
