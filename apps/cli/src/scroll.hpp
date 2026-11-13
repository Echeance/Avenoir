#pragma once

namespace avenoir::cli {

/// Pas de défilement de la conversation, du clavier ou de la souris.
enum class ScrollStep : unsigned char {
    WheelUp,   ///< Molette vers le haut : 3 lignes.
    WheelDown, ///< Molette vers le bas : 3 lignes.
    LineUp,    ///< Ctrl+↑ : une ligne.
    LineDown,  ///< Ctrl+↓ : une ligne.
    PageUp,    ///< PgUp : une page.
    PageDown,  ///< PgDn : une page.
    End,       ///< Retour en bas.
};

} // namespace avenoir::cli
