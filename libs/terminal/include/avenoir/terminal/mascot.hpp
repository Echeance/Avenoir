#pragma once

#include <array>
#include <span>
#include <string_view>

// Dessins d'Avenoir, en données seulement (aucune couleur ici) : le loup à
// l'épée et les lettres du titre « AVENOIR ». Source unique pour la CLI
// (en-tête, couleurs par zones), le démon (fond de l'écran de verrouillage)
// et les images du README (docs/readme/wolf.py lit ce fichier).
namespace avenoir::terminal {

/// Grand loup : 30 colonnes sur 18 lignes.
[[nodiscard]] std::span<const std::string_view> wolf_rows();
/// Zone de chaque caractère du grand loup : F loup, R manche, G garde,
/// B fil de la lame, C lame, espace : rien.
[[nodiscard]] std::span<const std::string_view> wolf_zones();
/// Petit loup (lame courte) : 27 colonnes sur 15 lignes.
[[nodiscard]] std::span<const std::string_view> wolf_icon_rows();
[[nodiscard]] std::span<const std::string_view> wolf_icon_zones();

/// Lettres de « AVENOIR » en blocs : 5 lignes de 5 colonnes par lettre
/// (« █ » plein, espace vide).
[[nodiscard]] std::span<const std::array<std::string_view, 5>> title_letters();

} // namespace avenoir::terminal
