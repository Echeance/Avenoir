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

/// Adresse d'un fichier d'onglet web : module et chemin d'adresse.
struct ModuleAddress {
    std::string module;
    std::string origin; ///< `avenoir-module://<id>` ou `https://<id>.module.avenoir`.
    std::string path;   ///< Commence par « / », sans requête ni fragment.
};

/// Analyse `avenoir-module://<module>/<chemin>` (Linux) ou
/// `https://<module>.module.avenoir/<chemin>` (Windows). Rien si l'adresse
/// n'a pas cette forme ou si l'identifiant n'est pas un identifiant de
/// module (`[a-z0-9][a-z0-9-]{0,31}`).
[[nodiscard]] std::optional<ModuleAddress> parse_module_address(std::string_view uri);

/// Politique de sécurité posée par l'hôte sur chaque fichier d'un onglet web
/// (directive 12) : les fichiers de son origine `origin` seulement
/// (`avenoir-module://<id>` ou `https://<id>.module.avenoir`), aucun réseau,
/// aucun formulaire, aucun cadre. L'origine est nommée plutôt que `'self'` :
/// dans une iframe isolée (origine opaque), WebKit ne fait pas correspondre
/// `'self'` à un schéma personnalisé.
[[nodiscard]] std::string module_tab_policy(std::string_view origin);

/// Type MIME d'après l'extension (html, js, css, svg, png, json, woff2), ou vide.
[[nodiscard]] std::string_view mime_type(std::string_view extension);

} // namespace avenoir::ui
