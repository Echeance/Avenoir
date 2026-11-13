#!/usr/bin/env python3
"""Génère le logo d'Avenoir en SVG, tel que l'en-tête de la CLI le dessine.

Le loup, ses zones, le petit loup et le titre « AVENOIR » en blocs sont lus
dans libs/terminal/src/mascot.cpp, source unique des dessins : le
logo du README et celui du terminal restent identiques. Le loup est « Wolf »
(asciiart.eu, artiste inconnu), tel quel, avec une épée ajoutée pour Avenoir.

Les couleurs sont celles de la capture (docs/readme/terminal_render.py) :
mêmes codes de terminal, même fond sombre, pour un rendu identique en thème
clair comme en thème sombre.

Usage : python3 docs/readme/wolf.py   (écrit docs/readme/avenoir-wolf.svg et
avenoir-wolf-icon.svg)
"""

import re
from html import escape
from pathlib import Path

MASCOT = Path(__file__).resolve().parents[2] / "libs" / "terminal" / "src" / "mascot.cpp"

# Fond et couleurs du terminal de la capture (terminal_render.py).
BACKGROUND = "#1e1e2e"
XTERM = {"navy": "#005faf", "azure": "#0087ff", "sky": "#87afff", "ice": "#afd7ff"}
WHITE, YELLOW, YELLOW_DIM, CYAN, GRAY = "#ffffff", "#f9e2af", "#958769", "#94e2d5", "#6c7086"

# Zones de wolf.cpp → (couleur, gras), comme zone_style().
ZONES = {
    "E": (CYAN, True), "N": (WHITE, False), "T": (WHITE, True), "B": (WHITE, True),
    "P": (YELLOW, True), "G": (YELLOW, True), "R": (YELLOW_DIM, False),
    "C": (XTERM["ice"], False), "Q": (GRAY, False), "F": (XTERM["sky"], False),
}
# Lignes du titre, du bleu clair au bleu nuit (title_art()).
TITLE_SHADES = ["ice", "sky", "azure", "azure", "navy"]
TITLE_ROW = 1  # le titre commence sur la deuxième ligne du loup, comme dans l'en-tête
GAP = 3        # colonnes entre le loup et le titre (side_by_side(..., 3))
SUBTITLE = "assistant de votre système"

CELL_W, CELL_H, FONT, PAD = 8.4, 17, 14, 18


def read_array(text: str, name: str) -> list[str]:
    block = re.search(rf" {name} = \{{(.*?)\}};", text, re.S).group(1)
    return re.findall(r'R"\((.*?)\)"', block)


def read_title(text: str) -> list[str]:
    block = re.search(r"kTitleLetters\{\{(.*?)\}\};", text, re.S).group(1)
    letters = [re.findall(r'"([^"]*)"', item) for item in re.findall(r"\{([^{}]*)\}", block)]
    return ["".join(letter[row] + " " for letter in letters).rstrip() for row in range(5)]


def x_of(column: float) -> float:
    return PAD + column * CELL_W


def y_of(row: int) -> float:
    return PAD + row * CELL_H


def text_row(row: int, column: int, text: str, zones: str) -> str:
    """Une ligne de texte, chaque suite d'une même zone dans son tspan.

    La ligne est calée sur la grille (`textLength`) : sans la police prévue,
    le dessin garde sa largeur et reste aligné avec le titre.
    """
    left = len(text) - len(text.lstrip(" "))
    text, zones = text.strip(" "), zones[left:left + len(text.strip(" "))]
    column += left
    spans, start = [], 0
    while start < len(text):
        end = start + 1
        while end < len(text) and zones[end] == zones[start]:
            end += 1
        piece = escape(text[start:end])
        if zones[start] == " ":
            spans.append(piece)
        else:
            color, bold = ZONES.get(zones[start], ZONES["F"])
            weight = ' font-weight="bold"' if bold else ""
            spans.append(f'<tspan fill="{color}"{weight}>{piece}</tspan>')
        start = end
    return (
        f'<text x="{x_of(column):.1f}" y="{y_of(row + 1) - 4:.1f}" '
        f'textLength="{len(text) * CELL_W:.1f}" lengthAdjust="spacingAndGlyphs">'
        f'{"".join(spans)}</text>'
    )


def block_rows(row: int, column: int, lines: list[str]) -> list[str]:
    """Les « █ » du titre en rectangles pleins, comme le terminal les dessine."""
    rects = []
    for offset, line in enumerate(lines):
        color = XTERM[TITLE_SHADES[offset]]
        for match in re.finditer(r"█+", line):
            rects.append(
                f'<rect x="{x_of(column + match.start()):.1f}" y="{y_of(row + offset):.1f}" '
                f'width="{len(match.group()) * CELL_W:.1f}" height="{CELL_H}" fill="{color}"/>'
            )
    return rects


def svg(columns: int, rows: int, body: list[str]) -> str:
    width, height = columns * CELL_W + 2 * PAD, rows * CELL_H + 2 * PAD
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.0f}" height="{height:.0f}" '
        f'viewBox="0 0 {width:.0f} {height:.0f}" role="img" '
        f'aria-label="Avenoir : le loup, une épée dans la gueule, en ASCII">\n'
        f"  <title>Avenoir</title>\n"
        f'  <rect width="100%" height="100%" rx="12" fill="{BACKGROUND}"/>\n'
        f'  <g font-family="JetBrains Mono, DejaVu Sans Mono, Menlo, Consolas, monospace" '
        f'font-size="{FONT}" xml:space="preserve" style="white-space: pre">\n    '
        + "\n    ".join(body)
        + "\n  </g>\n</svg>\n"
    )


def logo(art: list[str], zones: list[str], title: list[str]) -> str:
    """Le loup et le titre côte à côte, comme l'en-tête plein écran."""
    art_width = max(map(len, art))
    title_column = art_width + GAP
    body = [text_row(row, 0, text, zone) for row, (text, zone) in enumerate(zip(art, zones))]
    body += block_rows(TITLE_ROW, title_column, title)
    body.append(text_row(TITLE_ROW + len(title), title_column, SUBTITLE, "Q" * len(SUBTITLE)))
    columns = title_column + max(map(len, title))
    return svg(columns, len(art), body)


def icon(art: list[str], zones: list[str]) -> str:
    body = [text_row(row, 0, text, zone) for row, (text, zone) in enumerate(zip(art, zones))]
    return svg(max(map(len, art)), len(art), body)


if __name__ == "__main__":
    wolf = MASCOT.read_text(encoding="utf-8")
    title = read_title(wolf)
    outputs = {
        "avenoir-wolf.svg": logo(read_array(wolf, "kArt"), read_array(wolf, "kArtZones"), title),
        "avenoir-wolf-icon.svg": icon(read_array(wolf, "kIcon"), read_array(wolf, "kIconZones")),
    }
    for name, content in outputs.items():
        output = Path(__file__).with_name(name)
        output.write_text(content, encoding="utf-8")
        print(f"écrit : {output}")
