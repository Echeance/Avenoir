#!/usr/bin/env python3
"""Rejoue l'enregistrement d'une session de terminal et la dessine en PNG,
dans une fenêtre stylisée (pour les captures du README).

Émule ce qu'utilise la CLI d'Avenoir : texte UTF-8, couleurs (16, 256 et
24 bits, gras, atténué), déplacements du curseur (CUP, CUU, CUD, CHA),
effacements (ED, EL), zone de défilement (DECSTBM, SU, SD) et mémorisation
du curseur (ESC 7 / ESC 8). Les blocs ▀ ▄ █ sont dessinés en pixels.

Usage : terminal_render.py <enregistrement> <lignes> <colonnes> <image.png>
Polices : variables AVENOIR_FONT, AVENOIR_FONT_BOLD et AVENOIR_FONT_FALLBACK.
Dépendance : Pillow.
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

BACKGROUND = (30, 30, 46)
FOREGROUND = (205, 214, 244)
BASIC_COLORS = {
    30: (69, 71, 90), 31: (243, 139, 168), 32: (166, 227, 161), 33: (249, 226, 175),
    34: (137, 180, 250), 35: (245, 194, 231), 36: (148, 226, 213), 37: (186, 194, 222),
    90: (108, 112, 134), 91: (243, 139, 168), 92: (166, 227, 161), 93: (249, 226, 175),
    94: (137, 180, 250), 95: (245, 194, 231), 96: (148, 226, 213), 97: (255, 255, 255),
}
CSI = re.compile(r"\x1b\[(\??)([0-9;]*)([A-Za-z])")

FONT = os.environ.get("AVENOIR_FONT", "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Regular.ttf")
FONT_BOLD = os.environ.get("AVENOIR_FONT_BOLD", "/usr/share/fonts/TTF/JetBrainsMonoNerdFontMono-Bold.ttf")
FONT_FALLBACK = os.environ.get("AVENOIR_FONT_FALLBACK", "/usr/share/fonts/TTF/DejaVuSans.ttf")


class Screen:
    def __init__(self, rows: int, columns: int) -> None:
        self.rows, self.columns = rows, columns
        self.cells = [self.blank_line() for _ in range(rows)]
        self.row = self.column = 0
        self.top, self.bottom = 0, rows - 1
        self.pending_wrap = False
        self.saved = (0, 0)
        self.fg, self.bg, self.bold, self.dim = FOREGROUND, BACKGROUND, False, False

    def blank_line(self) -> list:
        return [(" ", FOREGROUND, BACKGROUND, False) for _ in range(self.columns)]

    def scroll_up(self) -> None:
        del self.cells[self.top]
        self.cells.insert(self.bottom, self.blank_line())

    def scroll_down(self) -> None:
        del self.cells[self.bottom]
        self.cells.insert(self.top, self.blank_line())

    def line_feed(self) -> None:
        if self.row == self.bottom:
            self.scroll_up()
        elif self.row < self.rows - 1:
            self.row += 1

    def select_graphic_rendition(self, params: list[int]) -> None:
        params = params or [0]
        i = 0
        while i < len(params):
            p = params[i]
            if p == 0:
                self.fg, self.bg, self.bold, self.dim = FOREGROUND, BACKGROUND, False, False
            elif p == 1:
                self.bold = True
            elif p == 2:
                self.dim = True
            elif p in BASIC_COLORS:
                self.fg = BASIC_COLORS[p]
            elif p == 39:
                self.fg = FOREGROUND
            elif p == 49:
                self.bg = BACKGROUND
            elif p in (38, 48) and i + 1 < len(params):
                if params[i + 1] == 2:
                    color = tuple(params[i + 2:i + 5])
                    i += 4
                else:
                    n = params[i + 2]
                    levels = (0, 95, 135, 175, 215, 255)  # cube 6×6×6 de xterm
                    if n >= 232:  # niveaux de gris
                        color = (8 + (n - 232) * 10,) * 3
                    elif n >= 16:
                        n -= 16
                        color = (levels[n // 36], levels[n // 6 % 6], levels[n % 6])
                    else:
                        color = FOREGROUND
                    i += 2
                if p == 38:
                    self.fg = color
                else:
                    self.bg = color
            i += 1

    def control(self, private: str, params: list[int], command: str) -> None:
        n = params[0] if params else 0
        if command == "m" and not private:
            self.select_graphic_rendition(params)
        elif command == "H":
            self.row = (params[0] or 1) - 1 if params else 0
            self.column = (params[1] - 1) if len(params) > 1 and params[1] else 0
        elif command == "A":
            self.row = max(0, self.row - (n or 1))
        elif command == "B":
            self.row = min(self.rows - 1, self.row + (n or 1))
        elif command == "G":
            self.column = (n or 1) - 1
        elif command == "J":
            if n == 2:
                self.cells = [self.blank_line() for _ in range(self.rows)]
            else:
                self.cells[self.row][self.column:] = self.blank_line()[self.column:]
                for y in range(self.row + 1, self.rows):
                    self.cells[y] = self.blank_line()
        elif command == "K":
            start = 0 if n == 2 else self.column
            self.cells[self.row][start:] = self.blank_line()[start:]
        elif command == "S":
            for _ in range(n or 1):
                self.scroll_up()
        elif command == "T":
            for _ in range(n or 1):
                self.scroll_down()
        elif private == "?" and 1049 in params and command in "hl":
            # Écran alterné : l'écran principal est mis de côté, puis rendu.
            if command == "h":
                self.main_screen = [list(line) for line in self.cells]
                self.cells = [self.blank_line() for _ in range(self.rows)]
            elif getattr(self, "main_screen", None) is not None:
                self.cells = self.main_screen
                self.main_screen = None
        elif command == "r" and not private:
            self.top = (params[0] - 1) if params and params[0] else 0
            self.bottom = min(self.rows - 1, (params[1] - 1) if len(params) > 1 and params[1] else self.rows - 1)
            self.row = self.column = 0
        self.row = max(0, min(self.rows - 1, self.row))
        self.column = max(0, min(self.columns - 1, self.column))
        self.pending_wrap = False

    def put(self, char: str) -> None:
        if self.pending_wrap:
            self.line_feed()
            self.column = 0
            self.pending_wrap = False
        fg = tuple(int(v * 0.6) for v in self.fg) if self.dim else self.fg
        self.cells[self.row][self.column] = (char, fg, self.bg, self.bold)
        if self.column == self.columns - 1:
            self.pending_wrap = True
        else:
            self.column += 1

    def feed(self, data: str) -> None:
        i = 0
        while i < len(data):
            char = data[i]
            if char == "\x1b":
                if data[i + 1:i + 2] == "7":
                    self.saved = (self.row, self.column)
                    i += 2
                    continue
                if data[i + 1:i + 2] == "8":
                    self.row, self.column = self.saved
                    self.pending_wrap = False
                    i += 2
                    continue
                match = CSI.match(data, i)
                if match:
                    private, raw, command = match.groups()
                    params = [int(x) if x else 0 for x in raw.split(";")] if raw else []
                    self.control(private, params, command)
                    i = match.end()
                    continue
                i += 1
                continue
            if char == "\r":
                self.column = 0
                self.pending_wrap = False
            elif char == "\n":
                self.line_feed()
                self.column = 0
                self.pending_wrap = False
            elif char >= " ":
                self.put(char)
            i += 1


def render(screen: Screen, output: str) -> None:
    cell_w, cell_h = 20, 40  # double résolution
    font = ImageFont.truetype(FONT, 32)
    font_bold = ImageFont.truetype(FONT_BOLD, 32)
    fallback = ImageFont.truetype(FONT_FALLBACK, 28)
    term = Image.new("RGB", (screen.columns * cell_w, screen.rows * cell_h), BACKGROUND)
    draw = ImageDraw.Draw(term)
    for y, line in enumerate(screen.cells):
        for x, (char, fg, bg, bold) in enumerate(line):
            left, top = x * cell_w, y * cell_h
            draw.rectangle([left, top, left + cell_w - 1, top + cell_h - 1], fill=bg)
            if char == "▀":
                draw.rectangle([left, top, left + cell_w - 1, top + cell_h // 2 - 1], fill=fg)
            elif char == "▄":
                draw.rectangle([left, top + cell_h // 2, left + cell_w - 1, top + cell_h - 1], fill=fg)
            elif char == "█":
                draw.rectangle([left, top, left + cell_w - 1, top + cell_h - 1], fill=fg)
            elif char != " ":
                main = font_bold if bold else font
                # Police de secours pour les caractères absents (⚙…).
                if char == "⚙" or main.getmask(char).getbbox() is None:
                    draw.text((left - 2, top + 4), char, fill=fg, font=fallback)
                else:
                    draw.text((left, top + 2), char, fill=fg, font=main)

    # Fenêtre : coins arrondis, barre de titre avec trois pastilles.
    pad, bar, radius = 40, 64, 28
    window = Image.new("RGBA", (term.width + 2 * pad, term.height + bar + pad), (0, 0, 0, 0))
    wd = ImageDraw.Draw(window)
    wd.rounded_rectangle([0, 0, window.width - 1, window.height - 1], radius=radius,
                         fill=BACKGROUND + (255,), outline=(69, 71, 90, 255), width=2)
    wd.rectangle([2, bar - 2, window.width - 3, bar - 1], fill=(49, 50, 68, 255))
    for k, color in enumerate([(243, 139, 168), (249, 226, 175), (166, 227, 161)]):
        cx, cy = 36 + k * 40, bar // 2
        wd.ellipse([cx - 11, cy - 11, cx + 11, cy + 11], fill=color + (255,))
    title_font = ImageFont.truetype(FONT, 26)
    title = "avenoir"
    wd.text(((window.width - wd.textlength(title, font=title_font)) / 2, bar / 2 - 16), title,
            fill=(147, 153, 178, 255), font=title_font)
    window.paste(term, (pad, bar))
    window.save(output)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    recording, rows, columns, image = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    screen = Screen(rows, columns)
    with open(recording, "rb") as f:
        screen.feed(f.read().decode("utf-8", "replace"))
    render(screen, image)
    print(f"écrit : {image}")
