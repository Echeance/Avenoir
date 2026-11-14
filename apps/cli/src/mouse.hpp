#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// Sélection de texte à la souris dans la conversation (plein écran) : la CLI
// lit la souris pour la molette, ce qui retire au terminal sa propre
// sélection ; Avenoir la fait donc lui-même et copie le texte choisi.
namespace avenoir::cli {

/// Événement du bouton gauche, en coordonnées du terminal (à partir de 1).
struct MouseEvent {
    enum class Kind : unsigned char {
        Press,   ///< Bouton enfoncé.
        Drag,    ///< Déplacement bouton tenu.
        Release, ///< Bouton relâché.
    };
    Kind kind = Kind::Press;
    std::size_t column = 1;
    std::size_t row = 1;
};

/// Événement du bouton gauche au codage SGR (`button` : 0 clic, +32
/// déplacement ; Maj, Méta et Ctrl ignorés), ou rien pour un autre bouton.
[[nodiscard]] std::optional<MouseEvent> left_button_event(unsigned int button, std::size_t column,
                                                          std::size_t row, bool release);

/// Séquence SGR complète (`ESC [ < bouton ; colonne ; ligne M|m`), ou rien
/// si elle est mal formée ou ne concerne pas le bouton gauche.
[[nodiscard]] std::optional<MouseEvent> parse_sgr_mouse(std::string_view sequence);

/// Partie d'une ligne sans styles comprise entre les colonnes d'affichage
/// `from` (incluse) et `to` (exclue), comptées à partir de 0 ; un caractère
/// large coupé est pris en entier.
[[nodiscard]] std::string columns_slice(std::string_view plain, std::size_t from, std::size_t to);

/// Base64 (RFC 4648), pour la copie par OSC 52.
[[nodiscard]] std::string base64(std::string_view bytes);

} // namespace avenoir::cli
