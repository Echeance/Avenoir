#include "avenoir/terminal/mascot.hpp"

#include <array>
#include <span>
#include <string_view>

namespace avenoir::terminal {

namespace {

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

constexpr std::array<std::array<std::string_view, 5>, 7> kTitleLetters{{
    {" ███ ", "█   █", "█████", "█   █", "█   █"}, // A
    {"█   █", "█   █", "█   █", " █ █ ", "  █  "}, // V
    {"█████", "█    ", "████ ", "█    ", "█████"}, // E
    {"█   █", "██  █", "█ █ █", "█  ██", "█   █"}, // N
    {" ███ ", "█   █", "█   █", "█   █", " ███ "}, // O
    {"█████", "  █  ", "  █  ", "  █  ", "█████"}, // I
    {"████ ", "█   █", "████ ", "█  █ ", "█   █"}, // R
}};

} // namespace

std::span<const std::string_view> wolf_rows() {
    return kArt;
}

std::span<const std::string_view> wolf_zones() {
    return kArtZones;
}

std::span<const std::string_view> wolf_icon_rows() {
    return kIcon;
}

std::span<const std::string_view> wolf_icon_zones() {
    return kIconZones;
}

std::span<const std::array<std::string_view, 5>> title_letters() {
    return kTitleLetters;
}

} // namespace avenoir::terminal
