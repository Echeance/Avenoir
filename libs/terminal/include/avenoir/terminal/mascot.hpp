#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Dessins d'Avenoir, en données seulement (aucune couleur ici) : le loup en
// braille et les lettres du titre « AVENOIR ». Source unique pour la CLI
// (en-tête, couleurs par zones), le démon (fond de l'écran de verrouillage)
// et les images du README (docs/readme/wolf.py lit ce fichier).
namespace avenoir::terminal {

/// Grand loup, en caractères braille (U+2800 à U+28FF) : 37 colonnes sur
/// 27 lignes.
[[nodiscard]] std::span<const std::string_view> wolf_rows();
/// Petit loup (le même, réduit de moitié) : 19 colonnes sur 14 lignes.
[[nodiscard]] std::span<const std::string_view> wolf_icon_rows();

/// Points de chaque cellule d'une ligne en braille, dans l'ordre : bit 0 à 2,
/// colonne de gauche de haut en bas ; bits 3 à 5, colonne de droite ; bit 6
/// en bas à gauche, bit 7 en bas à droite (codage Unicode). Un caractère qui
/// n'est pas du braille compte pour une cellule vide.
[[nodiscard]] std::vector<std::uint8_t> braille_cells(std::string_view row);

/// Lettres de « AVENOIR » en blocs : 5 lignes de 5 colonnes par lettre
/// (« █ » plein, espace vide).
[[nodiscard]] std::span<const std::array<std::string_view, 5>> title_letters();

} // namespace avenoir::terminal
