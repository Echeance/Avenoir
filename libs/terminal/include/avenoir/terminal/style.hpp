#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "avenoir/core/tool.hpp"

namespace avenoir::terminal {

enum class Color : std::uint8_t {
    Default,
    Gray,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    White, ///< Blanc vif.
    Cyan,
};

struct Style {
    Color color = Color::Default;
    bool bold = false;
    bool dim = false;
    bool italic = false;
    bool underline = false;
};

/// Détecte si la sortie standard est un terminal qui interprète les
/// séquences de contrôle (déplacements du curseur, zones de défilement) :
/// terminal interactif, `TERM` différent de `dumb`. Sous Windows, active le
/// traitement de ces séquences par la console.
[[nodiscard]] bool detect_vt_support();

/// Détecte si la sortie accepte les couleurs : séquences de contrôle
/// interprétées et `NO_COLOR` non défini (https://no-color.org).
[[nodiscard]] bool detect_color_support();

/// Active ou désactive les couleurs (et les animations) pour tout le programme.
void set_enabled(bool enabled) noexcept;
[[nodiscard]] bool enabled() noexcept;

/// `text` dans le style donné ; inchangé si les couleurs sont désactivées.
[[nodiscard]] std::string paint(std::string_view text, Style style);

/// Largeur affichée d'un texte UTF-8 (un caractère = une colonne ; les
/// séquences de couleur ne comptent pas).
[[nodiscard]] std::size_t display_width(std::string_view text);

/// Complète `text` par des espaces jusqu'à `width` colonnes.
[[nodiscard]] std::string pad(std::string_view text, std::size_t width);

/// Découpe `text` en lignes d'au plus `width` colonnes, aux espaces (un mot
/// plus long que `width` est coupé). Les retours à la ligne sont conservés.
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, std::size_t width);

/// Raccourcit `text` à `width` colonnes au plus, avec « … » s'il est coupé.
[[nodiscard]] std::string ellipsize(std::string_view text, std::size_t width);

/// Largeur du terminal en colonnes (80 si inconnue).
[[nodiscard]] std::size_t terminal_width();

/// Hauteur du terminal en lignes (24 si inconnue).
[[nodiscard]] std::size_t terminal_height();

/// Cadre arrondi de `width` colonnes, titre facultatif dans la bordure :
///   ╭─ titre ───╮
///   │ ligne     │
///   ╰───────────╯
[[nodiscard]] std::string box(std::string_view title, const std::vector<std::string>& lines,
                              Color border, std::size_t width);

/// Couleur 24 bits.
struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(const Rgb&) const = default;
};

/// Pixel art : chaque caractère de `pixels` désigne une couleur de `palette`
/// (« . » ou caractère absent = transparent). Deux lignes de pixels par ligne
/// de terminal, grâce aux demi-blocs ▀ et ▄ (texte = pixel du haut, fond =
/// pixel du bas) : les pixels sont à peu près carrés. Couleurs 24 bits si
/// `COLORTERM` les annonce, sinon les plus proches parmi 256. Lignes vides si
/// les couleurs sont désactivées.
[[nodiscard]] std::vector<std::string> pixel_art(const std::vector<std::string_view>& pixels,
                                                 const std::vector<std::pair<char, Rgb>>& palette);

/// Badge coloré du niveau de risque : « lecture seule » (vert), « réversible »
/// (jaune), « SENSIBLE » (rouge).
[[nodiscard]] std::string risk_badge(core::RiskLevel risk);

} // namespace avenoir::terminal
