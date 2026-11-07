#!/usr/bin/env python3
"""Génère le pingouin d'Avenoir en SVG (pixel art net à toutes les tailles).

La grille et la palette doivent rester identiques à celles du cadre d'accueil
de la CLI (apps/cli/src/display.cpp, Display::banner).

Usage : python3 docs/readme/penguin.py   (écrit docs/readme/avenoir-penguin.svg)
"""

from pathlib import Path

# K : corps bleu nuit, W : blanc, O : orange, « . » : transparent.
PENGUIN = [
    "....KKKKKK....", "...KKKKKKKK...", "..KKWWKKWWKK..", "..KKWKKKKWKK..",
    "..KKKKOOKKKK..", ".KKKWWOOWWKKK.", "KKKWWWWWWWWKKK", "KKWWWWWWWWWWKK",
    "KKWWWWWWWWWWKK", ".KWWWWWWWWWWK.", ".KKWWWWWWWWKK.", "..KKKWWWWKKK..",
    "..OOOO..OOOO..",
]
PALETTE = {"K": "#343f5c", "W": "#f0f0eb", "O": "#f5a028"}


def to_svg(pixels: list[str], scale: int = 12) -> str:
    rects = []
    for y, row in enumerate(pixels):
        x = 0
        while x < len(row):
            color = PALETTE.get(row[x])
            if color is None:
                x += 1
                continue
            # Pixels identiques consécutifs regroupés en un seul rectangle.
            start = x
            while x < len(row) and row[x] == row[start]:
                x += 1
            rects.append(f'<rect x="{start}" y="{y}" width="{x - start}" height="1" fill="{color}"/>')
    width, height = max(map(len, pixels)), len(pixels)
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
        f'width="{width * scale}" height="{height * scale}" shape-rendering="crispEdges" '
        f'role="img" aria-label="Pingouin d\'Avenoir en pixel art">\n'
        "  <title>Avenoir</title>\n  " + "\n  ".join(rects) + "\n</svg>\n"
    )


if __name__ == "__main__":
    output = Path(__file__).with_name("avenoir-penguin.svg")
    output.write_text(to_svg(PENGUIN), encoding="utf-8")
    print(f"écrit : {output}")
