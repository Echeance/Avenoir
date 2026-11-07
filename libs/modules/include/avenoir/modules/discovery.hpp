#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/modules/manifest.hpp"

namespace avenoir::modules {

/// Provenance d'un module : elle fixe son activation par défaut.
enum class Origin : std::uint8_t {
    Bundled, ///< Fourni avec Avenoir (`<installation>/modules/`) : activé par défaut.
    User,    ///< Installé par l'utilisateur : désactivé tant qu'il ne l'a pas activé.
};

struct SearchDirectory {
    std::filesystem::path path;
    Origin origin = Origin::User;
};

struct DiscoveredModule {
    Manifest manifest;
    Origin origin = Origin::User;
};

/// Dossier ignoré, et pourquoi (manifeste invalide, identifiant en double…).
struct DiscoveryProblem {
    std::filesystem::path directory;
    std::string message;
};

struct Discovery {
    std::vector<DiscoveredModule> modules;
    std::vector<DiscoveryProblem> problems;
};

/// Parcourt les dossiers dans l'ordre donné : un sous-dossier par module, avec
/// son `module.toml`. Rien n'est lancé. Un dossier absent est ignoré ; un
/// manifeste invalide est signalé sans empêcher les autres modules. Si deux
/// modules ont le même identifiant, le premier trouvé l'emporte (les modules
/// fournis passent avant ceux de l'utilisateur, qui ne peut donc pas les
/// remplacer en douce).
[[nodiscard]] Discovery discover(std::span<const SearchDirectory> directories);

/// Dossiers par défaut : `<dossier de l'exécutable>/modules` (modules fournis)
/// puis `<user_data>/modules` (modules de l'utilisateur), si connus.
[[nodiscard]] std::vector<SearchDirectory>
default_search_directories(const std::optional<std::filesystem::path>& user_data);

/// Dossier de l'exécutable en cours, ou rien s'il est introuvable.
[[nodiscard]] std::optional<std::filesystem::path> executable_directory();

/// Le module est-il activé ? Réglage explicite de l'utilisateur, sinon
/// activé seulement s'il est fourni avec Avenoir (directive de sécurité n° 15).
[[nodiscard]] bool is_enabled(const DiscoveredModule& module,
                              const core::ModuleSettings* settings) noexcept;

} // namespace avenoir::modules
