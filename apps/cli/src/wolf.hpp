#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace avenoir::cli {

/// Le loup d'Avenoir, dessiné en braille (libs/terminal mascot.cpp), en bleu
/// ciel ; en texte simple sans couleurs.

/// Grand loup : 37 colonnes sur 27 lignes (en-tête complet).
[[nodiscard]] std::vector<std::string> wolf_art();
/// Petit loup (réduit de moitié) : 19 colonnes sur 14 lignes (en-tête
/// compact, cadres).
[[nodiscard]] std::vector<std::string> wolf_icon();

/// Largeur du grand et du petit loup, en colonnes.
inline constexpr std::size_t kWolfArtWidth = 37;
inline constexpr std::size_t kWolfIconWidth = 19;

} // namespace avenoir::cli
