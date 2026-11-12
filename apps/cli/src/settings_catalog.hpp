#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"

// Réglages modifiables depuis Avenoir (/reglages, /reglage) : ce qui se
// décide sans terminal (testé à part). Les réglages sont scellés : c'est la
// seule façon de les changer (docs/PROJECT.md § 5.1).
namespace avenoir::cli {

/// Module connu (découvert), pour `modules.<id>.enabled`.
struct KnownModule {
    std::string id;
    bool bundled = false; ///< Fourni avec Avenoir (sinon : installé par l'utilisateur).
};

/// Ligne de /reglages.
struct SettingLine {
    std::string key;         ///< « agent.local_only »
    std::string value;       ///< Valeur actuelle, en français (« oui », « confirmer »…).
    std::string accepted;    ///< Valeurs acceptées (« oui | non »).
    std::string description; ///< À quoi il sert.
    bool security = false;   ///< Le changer peut affaiblir la sécurité.
};

/// Réglages modifiables et leur valeur dans `config`.
[[nodiscard]] std::vector<SettingLine> list_settings(const core::AppConfig& config,
                                                     const std::vector<KnownModule>& modules);

/// Changement demandé, validé.
struct SettingChange {
    core::AppConfig updated;
    std::string key;
    std::string before; ///< Valeur avant, en français.
    std::string after;  ///< Valeur après.
    /// Rempli si le changement affaiblit la sécurité ou la confidentialité :
    /// ce qu'il retire (montré avant l'accord « Je sais ce que je fais »).
    std::optional<std::string> weakening;
    /// Rempli si le changement renforce la sécurité : à appliquer tout de
    /// suite en relançant.
    bool strengthens = false;
};

/// `config` avec `key` = `value`. Erreur lisible si la clé est inconnue, la
/// valeur invalide ou interdite (`permissions.sensitive` jamais « autoriser »),
/// ou si la configuration obtenue ne serait plus valide.
[[nodiscard]] core::Result<SettingChange> change_setting(const core::AppConfig& config,
                                                         std::string_view key,
                                                         std::string_view value,
                                                         const std::vector<KnownModule>& modules);

} // namespace avenoir::cli
