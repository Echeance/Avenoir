#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Fichiers de l'interface servis à la page sous `avenoir://app/…` : seuls ceux
// du dossier de l'interface, jamais un autre fichier de la machine.
namespace avenoir::ui {

struct Asset {
    std::filesystem::path path;
    std::string mime;
};

/// Fichier de `root` désigné par le chemin d'adresse `uri_path`
/// (« /assets/index.js » ; « / » : `index.html`). Rien si le chemin sort de
/// `root` (« .. », lien symbolique), contient un caractère interdit, ne
/// désigne pas un fichier ordinaire ou a un type inconnu.
[[nodiscard]] std::optional<Asset> resolve_asset(const std::filesystem::path& root,
                                                 std::string_view uri_path);

/// Type MIME d'après l'extension (html, js, css, svg, png, json, woff2), ou vide.
[[nodiscard]] std::string_view mime_type(std::string_view extension);

} // namespace avenoir::ui
