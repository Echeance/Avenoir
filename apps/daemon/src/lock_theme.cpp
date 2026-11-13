#include "lock_theme.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/terminal/mascot.hpp"

namespace avenoir::daemon {

namespace {

// ─── Bureau ──────────────────────────────────────────────────────────────────

/// Minuscules ASCII (les noms de bureau le sont tous).
std::string lowered(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return out;
}

// ─── Image ───────────────────────────────────────────────────────────────────

// Palette : fond bleu nuit, puis les couleurs de la CLI (thème du README).
enum class Ink : std::uint8_t { Background, Sky, Grip, Gold, White, Ice, Azure, Navy };
constexpr std::array<std::uint32_t, 8> kPalette{0x0b1020, 0x87afff, 0x958769, 0xf9e2af,
                                                0xffffff, 0xafd7ff, 0x0087ff, 0x005faf};

/// Couleur d'une zone du loup (comme la CLI : zone_style).
Ink zone_ink(char zone) {
    switch (zone) {
    case 'R':
        return Ink::Grip;
    case 'G':
        return Ink::Gold;
    case 'B':
        return Ink::White;
    case 'C':
        return Ink::Ice;
    default:
        return Ink::Sky;
    }
}

/// Petite police 5 × 7 pour les seuls caractères du loup.
struct Glyph {
    char character = '\0';
    std::array<std::string_view, 7> rows;
};
constexpr std::array<Glyph, 12> kFont{{
    {.character = '/', .rows = {"....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."}},
    {.character = '\\', .rows = {"#....", ".#...", ".#...", "..#..", "...#.", "...#.", "....#"}},
    {.character = '|', .rows = {"..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {.character = '_', .rows = {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
    {.character = '=', .rows = {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {.character = '#', .rows = {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
    {.character = '.', .rows = {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {.character = 'V', .rows = {"#...#", "#...#", "#...#", ".#.#.", ".#.#.", "..#..", "..#.."}},
    {.character = '`', .rows = {".#...", "..#..", ".....", ".....", ".....", ".....", "....."}},
    {.character = '<', .rows = {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {.character = '(', .rows = {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {.character = ')', .rows = {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
}};

// Grille : une cellule de caractère fait 12 × 18 pixels, chaque point de la
// police 2 × 2 ; le titre est en blocs pleins d'une cellule, comme dans le
// terminal.
constexpr int kDot = 2;
constexpr int kCellWidth = 12;
constexpr int kCellHeight = 18;
constexpr int kTop = 24;       // haut du loup
constexpr int kTitleTop = 640; // haut du titre, sous l'anneau

class Canvas {
  public:
    Canvas(int width, int height)
        : width_(width), height_(height),
          pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                  static_cast<std::uint8_t>(Ink::Background)) {}

    void fill(int x, int y, int width, int height, Ink ink) {
        for (int row = std::max(y, 0); row < std::min(y + height, height_); ++row) {
            for (int column = std::max(x, 0); column < std::min(x + width, width_); ++column) {
                pixels_[(static_cast<std::size_t>(row) * static_cast<std::size_t>(width_)) +
                        static_cast<std::size_t>(column)] = static_cast<std::uint8_t>(ink);
            }
        }
    }

    /// Un caractère du loup dans la cellule (`x`, `y`) en pixels.
    void glyph(char character, int x, int y, Ink ink) {
        // Boucle simple : l'itérateur de std::array n'est pas un pointeur sous MSVC.
        const Glyph* found = nullptr;
        for (const auto& candidate : kFont) {
            if (candidate.character == character) {
                found = &candidate;
                break;
            }
        }
        if (found == nullptr) {
            return;
        }
        for (int row = 0; row < 7; ++row) {
            const std::string_view line = found->rows.at(static_cast<std::size_t>(row));
            for (int column = 0; column < 5; ++column) {
                if (line[static_cast<std::size_t>(column)] == '#') {
                    fill(x + (kDot / 2) + (column * kDot), y + 2 + (row * kDot), kDot, kDot, ink);
                }
            }
        }
    }

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] std::uint8_t at(int x, int y) const {
        return pixels_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)) +
                       static_cast<std::size_t>(x)];
    }

  private:
    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
};

// ─── PNG ─────────────────────────────────────────────────────────────────────

std::uint32_t crc32(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const char c : bytes) {
        crc ^= static_cast<std::uint8_t>(c);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (crc >> 1U) ^ 0xedb88320U : crc >> 1U;
        }
    }
    return crc ^ 0xffffffffU;
}

void put_u32(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out += static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xffU);
    }
}

void put_chunk(std::string& out, std::string_view type, std::string_view data) {
    put_u32(out, static_cast<std::uint32_t>(data.size()));
    std::string body(type);
    body += data;
    out += body;
    put_u32(out, crc32(body));
}

/// Flux zlib sans compression (blocs « stored ») : pas de bibliothèque.
std::string zlib_stored(std::string_view data) {
    constexpr std::size_t kBlock = 65535;
    std::string out{"\x78\x01"};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const char c : data) {
        a = (a + static_cast<std::uint8_t>(c)) % 65521U;
        b = (b + a) % 65521U;
    }
    std::size_t offset = 0;
    bool last = false;
    while (!last) {
        const std::size_t size = std::min(kBlock, data.size() - offset);
        last = offset + size == data.size();
        out += static_cast<char>(last ? 1 : 0);
        const auto length = static_cast<std::uint16_t>(size);
        const auto inverse = static_cast<std::uint16_t>(~length);
        out += static_cast<char>(length & 0xffU);
        out += static_cast<char>(length >> 8U);
        out += static_cast<char>(inverse & 0xffU);
        out += static_cast<char>(inverse >> 8U);
        out += data.substr(offset, size);
        offset += size;
    }
    put_u32(out, (b << 16U) | a);
    return out;
}

/// PNG à palette (8 bits par pixel).
std::string encode_png(const Canvas& canvas) {
    std::string png{"\x89PNG\r\n\x1a\n", 8};
    std::string header;
    put_u32(header, static_cast<std::uint32_t>(canvas.width()));
    put_u32(header, static_cast<std::uint32_t>(canvas.height()));
    header += std::string{"\x08\x03\x00\x00\x00", 5}; // 8 bits, palette
    put_chunk(png, "IHDR", header);
    std::string palette;
    for (const auto color : kPalette) {
        palette += static_cast<char>((color >> 16U) & 0xffU);
        palette += static_cast<char>((color >> 8U) & 0xffU);
        palette += static_cast<char>(color & 0xffU);
    }
    put_chunk(png, "PLTE", palette);
    std::string raw;
    raw.reserve(static_cast<std::size_t>(canvas.width() + 1) *
                static_cast<std::size_t>(canvas.height()));
    for (int y = 0; y < canvas.height(); ++y) {
        raw += '\0'; // filtre « aucun »
        for (int x = 0; x < canvas.width(); ++x) {
            raw += static_cast<char>(canvas.at(x, y));
        }
    }
    put_chunk(png, "IDAT", zlib_stored(raw));
    put_chunk(png, "IEND", "");
    return png;
}

} // namespace

bool desktop_locks_itself(std::string_view desktop) {
    const std::string name = lowered(desktop);
    constexpr std::array<std::string_view, 9> kSelfLocking{
        "gnome", "kde", "cinnamon", "xfce", "mate", "budgie", "unity", "pantheon", "lxqt"};
    return std::ranges::any_of(kSelfLocking,
                               [&name](std::string_view known) { return name.contains(known); });
}

bool lock_step_relevant(std::string_view session_type, std::string_view desktop) {
    return lowered(session_type) == "wayland" && !desktop_locks_itself(desktop);
}

std::string swaylock_config(std::string_view image_path) {
    // Un « : » dans le chemin serait lu comme un nom d'écran : préfixé d'un
    // autre « : » (man swaylock).
    const std::string image =
        image_path.contains(':') ? ":" + std::string(image_path) : std::string(image_path);
    return std::format(
        R"({}
# Écrit par Avenoir (création guidée ou /verrouillage). Vous pouvez le
# modifier : Avenoir ne le remplace que si vous le lui demandez.

# Fond bleu nuit : le loup à l'épée au-dessus de l'anneau, le titre dessous.
image={}
scaling=center
color=0b1020

# Anneau de saisie, au centre de l'écran.
indicator-radius=90
indicator-thickness=8
indicator-idle-visible
font=monospace
font-size=20
show-failed-attempts
ignore-empty-password

inside-color=0b1020
inside-clear-color=0b1020
inside-ver-color=0b1020
inside-wrong-color=0b1020
inside-caps-lock-color=0b1020
line-uses-inside
separator-color=00000000

# Bleu au repos, bleu glacier à la frappe, azur pendant la vérification,
# rose en cas d'erreur, or pour l'effacement et le verrouillage majuscule.
ring-color=005faf
ring-clear-color=87afff
ring-ver-color=0087ff
ring-wrong-color=f38ba8
ring-caps-lock-color=f9e2af
key-hl-color=afd7ff
bs-hl-color=f9e2af
caps-lock-key-hl-color=afd7ff
caps-lock-bs-hl-color=f9e2af

text-color=afd7ff
text-clear-color=87afff
text-ver-color=ffffff
text-wrong-color=f38ba8
text-caps-lock-color=f9e2af
layout-bg-color=00000000
layout-border-color=00000000
layout-text-color=87afff
)",
        kLockConfigMarker, image);
}

LockConfigState lock_config_state(const std::optional<std::string>& content) {
    if (!content) {
        return LockConfigState::Missing;
    }
    return content->starts_with(kLockConfigMarker) ? LockConfigState::Avenoir
                                                   : LockConfigState::Other;
}

std::string lock_image_png() {
    Canvas canvas(kLockImageWidth, kLockImageHeight);
    const auto rows = terminal::wolf_rows();
    const auto zones = terminal::wolf_zones();
    const auto letters = terminal::title_letters();
    const int wolf_columns = static_cast<int>(rows.front().size());
    const int title_columns = (static_cast<int>(letters.size()) * 6) - 1;
    const int left = (kLockImageWidth - (wolf_columns * kCellWidth)) / 2;

    // Le loup au-dessus de l'anneau, caractère par caractère, coloré par zones.
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const std::string_view text = rows[row];
        for (std::size_t column = 0; column < text.size(); ++column) {
            canvas.glyph(text[column], left + (static_cast<int>(column) * kCellWidth),
                         kTop + (static_cast<int>(row) * kCellHeight),
                         zone_ink(zones[row][column]));
        }
    }

    // Le titre en blocs sous l'anneau, du bleu glacier au bleu nuit.
    constexpr std::array<Ink, 5> kShades{Ink::Ice, Ink::Sky, Ink::Azure, Ink::Azure, Ink::Navy};
    const int title_left = (kLockImageWidth - (title_columns * kCellWidth)) / 2;
    for (std::size_t line = 0; line < 5; ++line) {
        int column = 0;
        for (const auto& letter : letters) {
            std::string_view cells = letter.at(line);
            while (!cells.empty()) {
                const bool full = cells.starts_with("█");
                if (full) {
                    canvas.fill(title_left + (column * kCellWidth),
                                kTitleTop + (static_cast<int>(line) * kCellHeight), kCellWidth,
                                kCellHeight, kShades.at(line));
                }
                cells.remove_prefix(full ? std::string_view("█").size() : 1);
                ++column;
            }
            ++column; // espace entre les lettres
        }
    }
    // Un filet sous le titre.
    canvas.fill(title_left, kTitleTop + (6 * kCellHeight), title_columns * kCellWidth, 2,
                Ink::Navy);
    return encode_png(canvas);
}

} // namespace avenoir::daemon
