#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/tool.hpp"

// Permissions durables (docs/PROJECT.md § 5, décision 61) : « toujours
// autoriser » ou « toujours refuser » un outil, choisi en répondant à une
// confirmation, enregistré dans les réglages scellés, révocable.
namespace avenoir::daemon {

/// Version du module de chaque outil de module chargé (nom complet → version).
using ToolVersions = std::map<std::string, std::string, std::less<>>;

/// Règles écrites (`[permissions.tools]`) à mettre en vigueur. Une règle
/// « toujours autoriser » d'un outil de module ne vaut que pour la version du
/// module à laquelle l'accord a été donné : sinon elle est écartée (la
/// confirmation revient) et `warnings` le dit. « Toujours refuser » vaut
/// toujours.
[[nodiscard]] core::ToolRuleMap active_rules(const core::ToolRuleMap& written,
                                             const ToolVersions& versions,
                                             std::vector<std::string>& warnings);

/// Règle posée par la réponse « toujours » (`Allow`) ou « jamais » (`Deny`)
/// à la confirmation de l'outil `spec` : risque actuel, version du module.
[[nodiscard]] core::ToolRule make_rule(core::Policy policy, const core::ToolSpec& spec,
                                       const ToolVersions& versions);

/// Règles retirées par /revoquer.
struct Revocation {
    core::AppConfig updated;
    std::vector<std::string> tools; ///< Outils dont la règle est retirée.
};

/// `config` sans la règle de `target` (nom d'outil), ou sans aucune règle
/// (« tout »). Erreur lisible si l'outil n'a pas de règle.
[[nodiscard]] core::Result<Revocation> revoke_rules(const core::AppConfig& config,
                                                    std::string_view target);

/// « toujours autorisé (réversible, v1.0.0) » : description d'une règle.
[[nodiscard]] std::string describe_rule(const core::ToolRule& rule);

/// Gardien des règles durables : celles en vigueur, partagées par tous les
/// agents, et leur enregistrement scellé. Le démon est le seul rédacteur des
/// réglages (directive 14).
class PermissionKeeper {
  public:
    /// Écriture scellée de la configuration (`ConfigFile::save`).
    using Save = std::function<core::Result<void>(const core::AppConfig&)>;

    /// `config` est la configuration du démon (tenue à jour ici), `save` son
    /// écriture, `versions` la version des outils de module chargés.
    PermissionKeeper(core::AppConfig& config, Save save, ToolVersions versions);

    /// Règles en vigueur, à donner aux agents.
    [[nodiscard]] std::shared_ptr<const core::ToolRules> rules() const { return rules_; }
    /// Avertissements de démarrage (règles écartées, actions sensibles
    /// toujours autorisées), vidés à la lecture.
    [[nodiscard]] std::vector<std::string> take_warnings();

    /// Réponse « toujours » (`Allow`) ou « jamais » (`Deny`) : enregistrée,
    /// en vigueur aussitôt pour tous les agents, auditée. Erreur si
    /// l'enregistrement échoue (rien n'est alors changé).
    [[nodiscard]] core::Result<void> remember(const core::ToolSpec& spec, core::Policy policy);

    /// /revoquer <outil | tout> : retire des règles (enregistré, en vigueur
    /// aussitôt, audité). Renvoie les outils concernés.
    [[nodiscard]] core::Result<std::vector<std::string>> revoke(std::string_view target);

    /// Règles écrites, telles qu'enregistrées.
    [[nodiscard]] core::ToolRuleMap written() const;
    /// Vrai si la règle de `tool` est écrite mais pas en vigueur.
    [[nodiscard]] bool inactive(std::string_view tool) const;

  private:
    /// Recalcule les règles en vigueur d'après `config_` (verrou tenu).
    void refresh(std::vector<std::string>& warnings);

    core::AppConfig* config_;
    Save save_;
    ToolVersions versions_;
    std::shared_ptr<core::ToolRules> rules_ = std::make_shared<core::ToolRules>();
    mutable std::mutex mutex_;
    std::vector<std::string> warnings_;
};

} // namespace avenoir::daemon
