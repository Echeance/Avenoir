#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace avenoir::cli {

/// Le loup d'Avenoir : « Wolf » (asciiart.eu), assis, qui hurle, avec une épée
/// dans la gueule (lame vers le haut à droite), en ASCII : coloré
/// par zones (pelage, yeux, lame, garde…), en texte simple sans couleurs.

/// Grand loup : 30 colonnes sur 18 lignes (en-tête, accueil).
[[nodiscard]] std::vector<std::string> wolf_art();
/// Petit loup (lame courte) : 27 colonnes sur 15 lignes (en-tête compact, cadres).
[[nodiscard]] std::vector<std::string> wolf_icon();

/// Largeur du grand et du petit loup, en colonnes.
inline constexpr std::size_t kWolfArtWidth = 30;
inline constexpr std::size_t kWolfIconWidth = 27;

} // namespace avenoir::cli
