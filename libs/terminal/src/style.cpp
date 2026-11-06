#include "avenoir/terminal/style.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/env.hpp"

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace avenoir::terminal {

namespace {

std::atomic<bool>& enabled_flag() {
    static std::atomic<bool> flag{false};
    return flag;
}

std::string_view color_code(Color color) {
    switch (color) {
    case Color::Default:
        return "";
    case Color::Gray:
        return "90";
    case Color::Red:
        return "31";
    case Color::Green:
        return "32";
    case Color::Yellow:
        return "33";
    case Color::Blue:
        return "34";
    case Color::White:
        return "97";
    case Color::Magenta:
        return "35";
    case Color::Cyan:
        return "36";
    }
    return "";
}

/// Répète une chaîne (caractère de cadre multioctet compris).
std::string repeat(std::string_view piece, std::size_t count) {
    std::string out;
    out.reserve(piece.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += piece;
    }
    return out;
}

} // namespace

bool detect_color_support() {
    return !core::read_env("NO_COLOR") && detect_vt_support();
}

bool detect_vt_support() {
    if (const auto term = core::read_env("TERM"); term && *term == "dumb") {
        return false;
    }
#ifdef _WIN32
    if (_isatty(_fileno(stdout)) == 0) {
        return false;
    }
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || GetConsoleMode(output, &mode) == 0) {
        return false;
    }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

void set_enabled(bool enabled) noexcept {
    enabled_flag() = enabled;
}

bool enabled() noexcept {
    return enabled_flag();
}

std::string paint(std::string_view text, Style style) {
    if (!enabled() || text.empty()) {
        return std::string(text);
    }
    std::vector<std::string_view> codes;
    if (style.bold) {
        codes.emplace_back("1");
    }
    if (style.dim) {
        codes.emplace_back("2");
    }
    if (style.italic) {
        codes.emplace_back("3");
    }
    if (style.underline) {
        codes.emplace_back("4");
    }
    if (const auto color = color_code(style.color); !color.empty()) {
        codes.push_back(color);
    }
    if (codes.empty()) {
        return std::string(text);
    }
    std::string out = "\x1b[";
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) {
            out += ';';
        }
        out += codes[i];
    }
    out += 'm';
    out += text;
    out += "\x1b[0m";
    return out;
}

std::size_t display_width(std::string_view text) {
    std::size_t width = 0;
    bool escape = false;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (escape) {
            // Séquence « ESC [ … lettre » : terminée par une lettre.
            if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')) {
                escape = false;
            }
            continue;
        }
        if (byte == 0x1BU) {
            escape = true;
            continue;
        }
        // Les octets de continuation UTF-8 (10xxxxxx) ne comptent pas.
        if ((byte & 0xC0U) != 0x80U) {
            ++width;
        }
    }
    return width;
}

std::string pad(std::string_view text, std::size_t width) {
    const std::size_t current = display_width(text);
    return std::string(text) + std::string(current < width ? width - current : 0, ' ');
}

std::vector<std::string> wrap(std::string_view text, std::size_t width) {
    std::vector<std::string> lines;
    if (width == 0) {
        lines.emplace_back(text);
        return lines;
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const std::string_view paragraph = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        start = end == std::string_view::npos ? text.size() + 1 : end + 1;

        std::string line;
        std::size_t word_start = 0;
        while (word_start <= paragraph.size()) {
            const auto space = paragraph.find(' ', word_start);
            std::string_view word = paragraph.substr(word_start, space == std::string_view::npos
                                                                     ? std::string_view::npos
                                                                     : space - word_start);
            word_start = space == std::string_view::npos ? paragraph.size() + 1 : space + 1;
            if (word.empty()) {
                continue;
            }
            const std::size_t needed =
                display_width(line) + (line.empty() ? 0 : 1) + display_width(word);
            if (!line.empty() && needed > width) {
                lines.push_back(std::move(line));
                line.clear();
            }
            // Mot trop long : coupé en morceaux de `width` colonnes.
            while (display_width(word) > width) {
                std::size_t bytes = 0;
                std::size_t columns = 0;
                while (bytes < word.size() && columns < width) {
                    ++bytes;
                    while (bytes < word.size() &&
                           (static_cast<unsigned char>(word[bytes]) & 0xC0U) == 0x80U) {
                        ++bytes;
                    }
                    ++columns;
                }
                lines.emplace_back(word.substr(0, bytes));
                word.remove_prefix(bytes);
            }
            if (!word.empty()) {
                line += (line.empty() ? "" : " ");
                line += word;
            }
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string truncate(std::string_view text, std::size_t width) {
    if (display_width(text) <= width) {
        return std::string(text);
    }
    std::string out;
    std::size_t columns = 0;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte & 0xC0U) != 0x80U) {
            if (columns + 1 >= width) {
                break;
            }
            ++columns;
        }
        out += c;
    }
    return out + "…";
}

std::size_t terminal_width() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        const auto columns = info.srWindow.Right - info.srWindow.Left + 1;
        if (columns > 0) {
            return static_cast<std::size_t>(columns);
        }
    }
#else
    winsize size{};
    // API système POSIX à arguments variables : pas d'alternative.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
#endif
    return 80;
}

std::size_t terminal_height() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        const auto rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (rows > 0) {
            return static_cast<std::size_t>(rows);
        }
    }
#else
    winsize size{};
    // API système POSIX à arguments variables : pas d'alternative.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
        return size.ws_row;
    }
#endif
    return 24;
}

std::string box(std::string_view title, const std::vector<std::string>& lines, Color border,
                std::size_t width) {
    const Style frame{.color = border};
    const std::size_t inner = width > 4 ? width - 4 : 0; // « │ » + texte + « │ »

    std::string top = "╭─";
    std::size_t used = 2;
    if (!title.empty()) {
        top += " " + std::string(title) + " ";
        used += display_width(title) + 2;
    }
    top += repeat("─", width > used + 1 ? width - used - 1 : 0) + "╮";

    std::string out = paint(top, frame) + "\n";
    for (const auto& line : lines) {
        out += paint("│", frame) + " " + pad(line, inner) + " " + paint("│", frame) + "\n";
    }
    out += paint("╰" + repeat("─", width > 2 ? width - 2 : 0) + "╯", frame);
    return out;
}

namespace {

bool truecolor() {
    const auto colorterm = core::read_env("COLORTERM");
    return colorterm && (*colorterm == "truecolor" || *colorterm == "24bit");
}

/// Couleur la plus proche dans le cube 6×6×6 des 256 couleurs.
int to_256(Rgb color) {
    const auto level = [](std::uint8_t value) { return ((value * 5) + 127) / 255; };
    return 16 + (36 * level(color.r)) + (6 * level(color.g)) + level(color.b);
}

std::string color_code(Rgb color, bool background, bool full) {
    if (full) {
        return std::format("\x1b[{};2;{};{};{}m", background ? 48 : 38, color.r, color.g, color.b);
    }
    return std::format("\x1b[{};5;{}m", background ? 48 : 38, to_256(color));
}

} // namespace

std::vector<std::string> pixel_art(const std::vector<std::string_view>& pixels,
                                   const std::vector<std::pair<char, Rgb>>& palette) {
    std::vector<std::string> lines;
    if (!enabled()) {
        return lines;
    }
    const bool full = truecolor();
    const auto lookup = [&palette](std::string_view row, std::size_t column) -> std::optional<Rgb> {
        if (column >= row.size()) {
            return std::nullopt;
        }
        const auto found = std::ranges::find(palette, row[column], &std::pair<char, Rgb>::first);
        return found == palette.end() ? std::nullopt : std::optional<Rgb>(found->second);
    };
    std::size_t width = 0;
    for (const auto row : pixels) {
        width = std::max(width, row.size());
    }
    for (std::size_t y = 0; y < pixels.size(); y += 2) {
        const std::string_view top = pixels[y];
        const std::string_view bottom = y + 1 < pixels.size() ? pixels[y + 1] : std::string_view{};
        std::string line;
        for (std::size_t x = 0; x < width; ++x) {
            const auto up = lookup(top, x);
            const auto down = lookup(bottom, x);
            if (up && down) {
                line += color_code(*up, false, full) + color_code(*down, true, full) + "▀";
            } else if (up) {
                line += color_code(*up, false, full) + "\x1b[49m▀";
            } else if (down) {
                line += color_code(*down, false, full) + "\x1b[49m▄";
            } else {
                line += "\x1b[0m ";
            }
        }
        line += "\x1b[0m";
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string risk_badge(core::RiskLevel risk) {
    switch (risk) {
    case core::RiskLevel::ReadOnly:
        return paint("lecture seule", {.color = Color::Green});
    case core::RiskLevel::Reversible:
        return paint("réversible", {.color = Color::Yellow});
    case core::RiskLevel::Sensitive:
        return paint("SENSIBLE", {.color = Color::Red, .bold = true});
    }
    return "inconnu";
}

} // namespace avenoir::terminal
