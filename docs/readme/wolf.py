#!/usr/bin/env python3
"""Génère le logo d'Avenoir en SVG, tel que l'en-tête de la CLI le dessine.

Le loup en braille, le petit loup et le titre « AVENOIR » en blocs sont lus
dans libs/terminal/src/mascot.cpp, source unique des dessins : le logo du
README et celui du terminal restent identiques. Chaque caractère braille est
dessiné point par point (2 × 4 points par cellule), comme sur l'écran de
verrouillage : le rendu ne dépend pas d'une police.

Les couleurs sont celles de la capture (docs/readme/terminal_render.py) :
mêmes codes de terminal ; le fond est transparent.

Usage : python3 docs/readme/wolf.py   (écrit docs/readme/avenoir-wolf.svg,
avenoir-wolf-icon.svg et ui/src/assets/wolf-dots.json)
"""

import json
import re
from html import escape
from pathlib import Path

MASCOT = Path(__file__).resolve().parents[2] / "libs" / "terminal" / "src" / "mascot.cpp"

# Couleurs du terminal de la capture (terminal_render.py) ; fond transparent :
# le logo se pose sur le thème clair comme sur le sombre.
XTERM = {"navy": "#005faf", "azure": "#0087ff", "sky": "#87afff", "ice": "#afd7ff"}
GRAY = "#6c7086"

# Lignes du titre, du bleu clair au bleu nuit (title_art()).
TITLE_SHADES = ["ice", "sky", "azure", "azure", "navy"]
DOT_RADIUS = 1.7
# Points du braille (bits Unicode) : (colonne, ligne) dans la cellule.
BRAILLE = [(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (0, 3), (1, 3)]
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


def text_row(row: int, column: int, text: str, color: str) -> str:
    """Une ligne de texte, calée sur la grille (`textLength`)."""
    return (
        f'<text x="{x_of(column):.1f}" y="{y_of(row + 1) - 4:.1f}" fill="{color}" '
        f'textLength="{len(text) * CELL_W:.1f}" lengthAdjust="spacingAndGlyphs">'
        f"{escape(text)}</text>"
    )


def dots(art: list[str]) -> dict:
    """Points levés du braille : colonnes et lignes de points (2 × 4 par
    caractère), et la liste [x, y] de chaque point."""
    points = []
    for row, line in enumerate(art):
        for column, character in enumerate(line):
            bits = ord(character) - 0x2800
            if 0 <= bits <= 0xFF:
                points += [[column * 2 + dx, row * 4 + dy]
                           for bit, (dx, dy) in enumerate(BRAILLE) if bits >> bit & 1]
    return {"width": max(map(len, art)) * 2, "height": len(art) * 4, "dots": points}


def braille_dots(art: list[str]) -> list[str]:
    """Le loup point par point : un cercle par point levé du braille."""
    dots = []
    for row, line in enumerate(art):
        for column, character in enumerate(line):
            bits = ord(character) - 0x2800
            if not 0 <= bits <= 0xFF:
                continue
            for bit, (dx, dy) in enumerate(BRAILLE):
                if bits >> bit & 1:
                    cx = x_of(column) + (dx + 0.5) * CELL_W / 2
                    cy = y_of(row) + (dy + 0.5) * CELL_H / 4
                    dots.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{DOT_RADIUS}"/>')
    return [f'<g fill="{XTERM["sky"]}">'] + dots + ["</g>"]


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
        f'aria-label="Avenoir : le loup d\'Avenoir, dessiné en braille">\n'
        f"  <title>Avenoir</title>\n"
        f'  <g font-family="JetBrains Mono, DejaVu Sans Mono, Menlo, Consolas, monospace" '
        f'font-size="{FONT}" xml:space="preserve" style="white-space: pre">\n    '
        + "\n    ".join(body)
        + "\n  </g>\n</svg>\n"
    )


def logo(art: list[str], title: list[str]) -> str:
    """Le loup et le titre côte à côte, comme l'en-tête plein écran (titre
    centré sur la hauteur du loup)."""
    art_width = max(map(len, art))
    title_column = art_width + GAP
    title_row = (len(art) - len(title) - 1) // 2
    body = braille_dots(art)
    body += block_rows(title_row, title_column, title)
    body.append(text_row(title_row + len(title), title_column, SUBTITLE, GRAY))
    columns = title_column + max(map(len, title))
    return svg(columns, len(art), body)


def icon(art: list[str]) -> str:
    return svg(max(map(len, art)), len(art), braille_dots(art))


if __name__ == "__main__":
    wolf = MASCOT.read_text(encoding="utf-8")
    title = read_title(wolf)
    outputs = {
        "avenoir-wolf.svg": logo(read_array(wolf, "kArt"), title),
        "avenoir-wolf-icon.svg": icon(read_array(wolf, "kIcon")),
    }
    # Le petit loup sert aussi à l'interface graphique : ses points, que la
    # page anime (ui/src/components/Wolf.svelte).
    ui_dots = Path(__file__).resolve().parents[2] / "ui" / "src" / "assets" / "wolf-dots.json"
    ui_dots.write_text(json.dumps(dots(read_array(wolf, "kIcon"))) + "\n", encoding="utf-8")
    print(f"écrit : {ui_dots}")
    for name, content in outputs.items():
        output = Path(__file__).with_name(name)
        output.write_text(content, encoding="utf-8")
        print(f"écrit : {output}")
