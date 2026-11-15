#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/modules/manifest.hpp"

// Installation, mise à jour et désinstallation des modules de l'utilisateur
// (docs/PROJECT.md § 2.6, décision 77). Un module qui tourne ne peut être ni
// remplacé ni supprimé (Windows verrouille ses fichiers) : la commande prépare
// le changement — copie dans `<modules>/.<id>.nouveau`, marque
// `<modules>/<id>/.desinstaller` —, Avenoir redémarre, et `apply_pending`
// l'applique avant de lancer les modules. Les dossiers commençant par « . »
// sont ignorés par la découverte.
namespace avenoir::modules {

/// Fichiers d'un module au plus, et taille totale au plus.
inline constexpr std::size_t kMaxModuleFiles = 2000;
inline constexpr std::uintmax_t kMaxModuleBytes = std::uintmax_t{200} * 1024 * 1024;

/// Ce qu'installer le dossier `source` changerait.
struct InstallPlan {
    Manifest manifest;                 ///< Celui du dossier à installer.
    std::filesystem::path source;      ///< Chemin canonique.
    std::optional<Manifest> installed; ///< Version déjà installée, s'il y en a une.
    std::vector<std::string> added;    ///< Permissions demandées en plus.
    std::size_t files = 0;
    std::uintmax_t bytes = 0;
};

/// Vérifie le dossier `source` (son `module.toml`, nommé comme le module) :
/// manifeste valide, ni lien symbolique ni fichier spécial, `kMaxModuleFiles`
/// fichiers et `kMaxModuleBytes` au plus, hors de `user_root`, identifiant qui
/// n'est pas celui d'un module fourni (`reserved`). Ne modifie rien.
[[nodiscard]] core::Result<InstallPlan> prepare_install(const std::filesystem::path& source,
                                                        const std::filesystem::path& user_root,
                                                        std::span<const std::string> reserved);

/// Copie le module dans `<user_root>/.<id>.nouveau` (en remplaçant une copie
/// précédente), appliquée au prochain démarrage.
[[nodiscard]] core::Result<void> stage_install(const InstallPlan& plan,
                                               const std::filesystem::path& user_root);

/// Marque `<user_root>/<id>` pour la désinstallation au prochain démarrage,
/// avec ses données (`<data_root>/<id>`) si `erase_data`.
[[nodiscard]] core::Result<void> mark_uninstall(const std::filesystem::path& user_root,
                                                std::string_view id, bool erase_data);

/// Applique les changements préparés (mises en place, puis désinstallations)
/// et rend un message par changement. Rien ne doit tourner depuis ces dossiers.
[[nodiscard]] std::vector<std::string>
apply_pending(const std::filesystem::path& user_root,
              const std::optional<std::filesystem::path>& data_root);

} // namespace avenoir::modules
