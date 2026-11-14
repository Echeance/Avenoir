#include "avenoir/terminal/mascot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace avenoir::terminal {

namespace {

// Le loup d'Avenoir, dessiné en braille (dessin donné par l'auteur) : un
// caractère par cellule, chacun une grille de 2 × 4 points.
constexpr std::array<std::string_view, 27> kArt = {
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣤⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣰⠻⣥⠙⢦⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⡿⠀⡿⠻⣆⠙⠦⣤⣀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⢿⡄⠁⠀⠘⣆⡔⢶⣆⠉⢷⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢻⡄⠀⠀⡿⢿⡀⠉⠀⠞⠹⡆⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⡿⡄⠀⡇⠘⣧⣀⣀⣀⠀⠻⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣼⠃⠁⢀⣠⠞⣹⢿⠻⡟⢿⣿⣯⢳⡄⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣠⣾⠃⠶⠒⠉⠁⣴⠇⢸⡇⡟⡷⢬⡙⠎⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣴⣿⠇⢀⣠⣄⡀⠚⠁⠀⠈⠀⠀⣷⠀⠉⠛⠛⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⡼⣽⣿⣶⠋⢉⡿⠇⠀⠀⠀⠀⠀⣰⣿⣇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⢱⣿⣿⠇⠀⣠⣥⣤⡀⠀⠀⠀⢀⡟⣿⣿⣦⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⣼⢿⣿⢀⣾⡟⠉⢹⡇⠀⠀⠀⢸⠁⡿⠙⣿⣷⣄⡀⠀⠀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠸⢸⣇⣾⡟⠀⠸⡏⣄⡀⠀⠀⢹⢀⡇⢀⢘⢿⣮⡙⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⣿⣿⣇⠀⡀⣧⠰⣿⣶⣄⠀⠀⠀⠘⣎⠳⣿⣿⣦⡀⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠸⡿⣿⣆⠹⣿⡐⣾⣷⣹⣆⠀⠀⠀⠘⢷⣄⣻⣿⣿⣷⡄⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠹⢿⣿⣦⠽⣇⣹⣟⢿⠙⠁⠀⠀⠀⣤⠉⠻⣿⣿⣿⣿⣦⡀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⠙⡟⠂⣿⢹⡿⣼⠇⠀⠀⣀⠀⣷⡀⠀⠈⠻⣿⣿⣿⣷⡀⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢰⡆⢻⠀⠉⢸⡇⠈⣀⣠⣾⠇⠀⠻⣿⣦⣤⣴⣿⠿⣿⡿⣷⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⡇⢸⡀⠀⢸⠁⣰⠛⣽⡧⠖⠻⢿⡆⠈⠉⠉⠀⠀⢻⣷⠹⠇)", R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣿⠘⡇⠀⢸⢰⡏⢰⡟⠀⣀⣀⡼⠃⠀⢀⡆⠀⠀⠘⣿⡆⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣠⣤⣴⣿⣶⣷⣶⣾⣿⣧⣾⣤⣄⣀⣀⣤⣤⣶⡿⠀⠀⠀⢠⣿⡇⠀)", R"(⠀⠀⠀⠀⠀⠀⠀⣠⣴⣾⣿⣟⡛⠛⠛⠉⠉⠉⠉⢉⣭⣽⡿⠿⠿⠿⠛⠛⠛⠓⠲⠦⠄⣼⢻⡇⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠘⢉⣼⣿⣿⠿⠛⠛⠁⠀⠀⣠⠖⠋⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡸⠁⣸⡇⠀)", R"(⠀⠀⠀⠀⠀⢀⣴⠿⠛⠁⢀⣀⣀⣀⣀⣀⣄⡀⠀⠀⠀⢦⣀⠀⠀⠀⠀⠀⠀⠀⠀⣠⠇⣰⣿⠁⠀)",
    R"(⠀⠀⠀⢀⣴⣟⣥⣶⣾⣿⣿⣿⣿⣿⣿⣭⣤⣤⣤⣀⣀⡀⠈⠛⠶⢶⣶⣶⣶⣾⣿⣿⣿⠟⠁⠀⠀)", R"(⠀⢀⣴⡿⠟⠋⡽⠟⠉⠉⠀⠀⠀⠀⠀⠀⠀⠈⠉⠉⠉⠙⠛⠛⠛⠿⠿⠿⠿⠟⠛⠉⠁⠀⠀⠀⠀)",
    R"(⠐⠋⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
};

// Le même, réduit de moitié (un point gardé si l'un des quatre l'était).
constexpr std::array<std::string_view, 14> kIcon = {
    R"(⠀⠀⠀⠀⠀⠀⣴⡶⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠸⣇⠟⢯⣷⡶⣄⠀⠀⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠀⢘⣆⢸⢷⣁⡛⣇⠀⠀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠀⢀⡾⡭⠶⣿⣿⣿⣿⣷⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⢠⣿⡷⣶⡎⠁⠁⣼⡌⠉⠀⠀⠀⠀⠀)", R"(⠀⠀⠀⠀⢸⣿⣣⡾⣶⠀⢰⢿⢿⣄⡀⠀⠀⠀⠀)",
    R"(⠀⠀⠀⠀⠘⢿⣿⢘⣷⣦⡘⠚⢾⣿⣧⡀⠀⠀⠀)", R"(⠀⠀⠀⠀⠀⠘⣿⣿⣿⣿⡿⠄⠈⡳⣿⣿⣦⡀⠀)", R"(⠀⠀⠀⠀⠀⠀⢨⣽⠹⣿⢟⣠⡆⣷⣌⣿⣿⣷⠀)",
    R"(⠀⠀⠀⠀⠀⠀⠈⡿⡆⣯⣯⡟⣛⡿⢉⡅⠸⣿⠃)", R"(⠀⠀⠀⢀⣤⣶⡾⠿⠿⢿⣿⣶⣶⠶⠿⣤⣰⣿⠀)", R"(⠀⠀⢀⣽⠿⣛⣉⣁⣐⠋⢡⡀⠀⠀⠀⢀⣯⡿⠀)",
    R"(⢀⣴⡿⣿⠿⠛⠛⠛⠲⠶⠶⠭⢿⣿⣿⠿⠟⠁⠀)", R"(⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀)",
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

std::span<const std::string_view> wolf_icon_rows() {
    return kIcon;
}

std::vector<std::uint8_t> braille_cells(std::string_view row) {
    std::vector<std::uint8_t> cells;
    std::size_t at = 0;
    while (at < row.size()) {
        const auto lead = static_cast<unsigned char>(row[at]);
        // U+2800 à U+28FF : E2 A0..A3 xx en UTF-8.
        if (lead == 0xE2U && at + 2 < row.size()) {
            const auto second = static_cast<unsigned char>(row[at + 1]);
            const auto third = static_cast<unsigned char>(row[at + 2]);
            if (second >= 0xA0U && second <= 0xA3U) {
                cells.push_back(
                    static_cast<std::uint8_t>(((second & 0x03U) << 6U) | (third & 0x3FU)));
                at += 3;
                continue;
            }
        }
        cells.push_back(0);
        // Un caractère, quelle que soit sa longueur en UTF-8.
        ++at;
        while (at < row.size() && (static_cast<unsigned char>(row[at]) & 0xC0U) == 0x80U) {
            ++at;
        }
    }
    return cells;
}

std::span<const std::array<std::string_view, 5>> title_letters() {
    return kTitleLetters;
}

} // namespace avenoir::terminal
