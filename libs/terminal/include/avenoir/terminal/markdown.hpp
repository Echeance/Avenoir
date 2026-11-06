#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace avenoir::terminal {

/// Rendu Markdown léger pour le terminal, ligne par ligne :
/// - `**gras**`, `*italique*`, `` `code` ``, liens `[texte](url)` ;
/// - titres `#`, listes `-`/`*`/`+` (puces •), listes numérotées, citations `>` ;
/// - blocs de code délimités par trois accents graves (affichés en couleur,
///   sans interprétation) ; ligne horizontale `---`.
/// Les marqueurs sont retirés même sans couleurs : le texte reste lisible.
[[nodiscard]] std::vector<std::string> render_markdown(std::string_view text);

} // namespace avenoir::terminal
