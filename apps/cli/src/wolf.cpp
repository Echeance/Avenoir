#include "wolf.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/terminal/mascot.hpp"
#include "avenoir/terminal/style.hpp"

namespace avenoir::cli {

namespace {

using terminal::Color;
using terminal::Style;

Style zone_style(char zone) {
    switch (zone) {
    case 'E':
        return {.color = Color::Cyan, .bold = true};
    case 'N':
        return {.color = Color::White};
    case 'T':
    case 'B':
        return {.color = Color::White, .bold = true};
    case 'P':
    case 'G':
        return {.color = Color::Yellow, .bold = true};
    case 'R':
        return {.color = Color::Yellow, .dim = true};
    case 'C':
        return {.color = Color::Ice};
    case 'Q':
        return {.color = Color::Gray};
    default:
        return {.color = Color::Sky};
    }
}

/// Lignes du dessin, chaque suite de caractères d'une même zone peinte d'un trait.
std::vector<std::string> colored(std::span<const std::string_view> art,
                                 std::span<const std::string_view> zones) {
    std::vector<std::string> lines;
    for (std::size_t row = 0; row < art.size(); ++row) {
        const std::string_view text = art[row];
        const std::string_view zone = zones[row];
        std::string line;
        std::size_t start = 0;
        while (start < text.size()) {
            std::size_t end = start + 1;
            while (end < text.size() && zone[end] == zone[start]) {
                ++end;
            }
            const std::string_view piece = text.substr(start, end - start);
            line += zone[start] == ' ' ? std::string(piece)
                                       : terminal::paint(piece, zone_style(zone[start]));
            start = end;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace

std::vector<std::string> wolf_art() {
    return colored(terminal::wolf_rows(), terminal::wolf_zones());
}

std::vector<std::string> wolf_icon() {
    return colored(terminal::wolf_icon_rows(), terminal::wolf_icon_zones());
}

} // namespace avenoir::cli
