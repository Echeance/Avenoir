#include "wolf.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/terminal/mascot.hpp"
#include "avenoir/terminal/style.hpp"

namespace avenoir::cli {

namespace {

/// Le dessin, d'une seule couleur : le bleu ciel de l'en-tête.
std::vector<std::string> colored(std::span<const std::string_view> art) {
    std::vector<std::string> lines;
    lines.reserve(art.size());
    for (const auto row : art) {
        lines.push_back(terminal::paint(row, {.color = terminal::Color::Sky}));
    }
    return lines;
}

} // namespace

std::vector<std::string> wolf_art() {
    return colored(terminal::wolf_rows());
}

std::vector<std::string> wolf_icon() {
    return colored(terminal::wolf_icon_rows());
}

} // namespace avenoir::cli
