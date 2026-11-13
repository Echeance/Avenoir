#include "wolf.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/terminal/style.hpp"

namespace avenoir::cli {

namespace {

using terminal::Color;
using terminal::Style;

// « Wolf » (asciiart.eu, artiste inconnu), loup assis qui hurle, tel quel :
// seule l'épée est ajoutée pour Avenoir, le manche dans la gueule ouverte et
// la lame vers le haut à droite.
constexpr std::array<std::string_view, 18> kArt = {
    R"(                            /\)", R"(                           / /)",
    R"(                          / / )", R"(                         / /  )",
    R"(                        / /   )", R"(                       / /    )",
    R"(                     =##=     )", R"(                     ./       )",
    R"(                    / V\      )", R"(                  / `  /      )",
    R"(                 <<   |       )", R"(                 /    |       )",
    R"(               /      |       )", R"(             /        |       )",
    R"(           /    \  \ /        )", R"(          (      ) | |        )",
    R"(  ________|   _/_  | |        )", R"(<__________\______)\__)       )",
};

/// Zone de chaque caractère de `kArt` : F loup, R manche, G garde, B fil de
/// la lame, C lame.
constexpr std::array<std::string_view, 18> kArtZones = {
    R"(                            BB)", R"(                           B C)",
    R"(                          B C )", R"(                         B C  )",
    R"(                        B C   )", R"(                       B C    )",
    R"(                     GGGG     )", R"(                     FR       )",
    R"(                    F FF      )", R"(                  F F  F      )",
    R"(                 FF   F       )", R"(                 F    F       )",
    R"(               F      F       )", R"(             F        F       )",
    R"(           F    F  F F        )", R"(          F      F F F        )",
    R"(  FFFFFFFFF   FFF  F F        )", R"(FFFFFFFFFFFFFFFFFFFFFFF       )",
};

constexpr std::array<std::string_view, 15> kIcon = {
    R"(                         /\)", R"(                        / /)",
    R"(                       / / )", R"(                     =##=  )",
    R"(                     ./    )", R"(                    / V\   )",
    R"(                  / `  /   )", R"(                 <<   |    )",
    R"(                 /    |    )", R"(               /      |    )",
    R"(             /        |    )", R"(           /    \  \ /     )",
    R"(          (      ) | |     )", R"(  ________|   _/_  | |     )",
    R"(<__________\______)\__)    )",
};

constexpr std::array<std::string_view, 15> kIconZones = {
    R"(                         BB)", R"(                        B C)",
    R"(                       B C )", R"(                     GGGG  )",
    R"(                     FR    )", R"(                    F FF   )",
    R"(                  F F  F   )", R"(                 FF   F    )",
    R"(                 F    F    )", R"(               F      F    )",
    R"(             F        F    )", R"(           F    F  F F     )",
    R"(          F      F F F     )", R"(  FFFFFFFFF   FFF  F F     )",
    R"(FFFFFFFFFFFFFFFFFFFFFFF    )",
};

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
    return colored(kArt, kArtZones);
}

std::vector<std::string> wolf_icon() {
    return colored(kIcon, kIconZones);
}

} // namespace avenoir::cli
