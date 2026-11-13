#!/usr/bin/env python3
"""Génère le loup d'Avenoir en SVG : le dessin ASCII de la CLI, coloré par zones.

Le dessin, ses zones et le petit loup sont lus dans apps/cli/src/wolf.cpp :
le logo du README et le loup du terminal restent identiques. Le loup est
« Wolf » (asciiart.eu, artiste inconnu), tel quel, avec une épée ajoutée
pour Avenoir.

Usage : python3 docs/readme/wolf.py   (écrit docs/readme/avenoir-wolf.svg et
avenoir-wolf-icon.svg)
"""

import re
from html import escape
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[2] / "apps" / "cli" / "src" / "wolf.cpp"

# Couleurs des zones (proches de celles de la CLI sur un terminal sombre).
COLORS = {
    "F": "#87afd7", "E": "#00d7ff", "N": "#eeeeee", "T": "#ffffff", "P": "#d7af5f",
    "G": "#d7af5f", "R": "#875f00", "B": "#ffffff", "C": "#afd7ff",
    "Q": "#808080",
}
BOLD = {"E", "T", "B", "P", "G"}
CELL_W, CELL_H, FONT = 8.4, 17, 14


def read_array(text: str, name: str) -> list[str]:
    block = re.search(rf" {name} = \{{(.*?)\}};", text, re.S).group(1)
    return re.findall(r'R"\((.*?)\)"', block)


def to_svg(art: list[str], zones: list[str]) -> str:
    width, height = max(map(len, art)), len(art)
    lines = []
    for y, (text, zone) in enumerate(zip(art, zones)):
        spans, x = [], 0
        while x < len(text):
            end = x + 1
            while end < len(text) and zone[end] == zone[x]:
                end += 1
            piece = escape(text[x:end])
            if zone[x] in COLORS:
                weight = ' font-weight="bold"' if zone[x] in BOLD else ""
                spans.append(f'<tspan fill="{COLORS[zone[x]]}"{weight}>{piece}</tspan>')
            else:
                spans.append(piece)
            x = end
        lines.append(f'<text x="0" y="{(y + 1) * CELL_H - 4}">{"".join(spans)}</text>')
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width * CELL_W:.0f}" '
        f'height="{height * CELL_H}" role="img" '
        f'aria-label="Le loup d\'Avenoir, une épée dans la gueule, en ASCII">\n'
        f'  <title>Avenoir</title>\n'
        f'  <g font-family="DejaVu Sans Mono, Menlo, Consolas, monospace" font-size="{FONT}" '
        f'xml:space="preserve">\n    ' + "\n    ".join(lines) + "\n  </g>\n</svg>\n"
    )


if __name__ == "__main__":
    source = SOURCE.read_text(encoding="utf-8")
    for name, art, zones in (
        ("avenoir-wolf.svg", "kArt", "kArtZones"),
        ("avenoir-wolf-icon.svg", "kIcon", "kIconZones"),
    ):
        output = Path(__file__).with_name(name)
        output.write_text(to_svg(read_array(source, art), read_array(source, zones)),
                          encoding="utf-8")
        print(f"écrit : {output}")
