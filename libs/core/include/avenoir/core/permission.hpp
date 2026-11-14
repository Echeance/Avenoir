#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::core {

enum class Policy : std::uint8_t {
    Allow,
    Confirm,
    Deny,
};

[[nodiscard]] std::string_view to_string(Policy policy) noexcept;
/// « allow », « confirm » ou « deny ».
[[nodiscard]] std::optional<Policy> policy_from_string(std::string_view text) noexcept;

/// Règle durable d'un outil (`[permissions.tools]`), posée par l'utilisateur
/// en répondant « toujours » ou « jamais » à une confirmation, révocable.
struct ToolRule {
    /// `Allow` (« toujours autoriser ») ou `Deny` (« toujours refuser »).
    Policy policy = Policy::Deny;
    /// Risque de l'outil quand « toujours autoriser » a été choisi : la règle
    /// ne vaut plus si ce risque est relevé depuis.
    RiskLevel risk = RiskLevel::Sensitive;
    /// Outil de module : version du module à laquelle l'accord a été donné ;
    /// une autre version ne profite pas de la règle.
    std::optional<std::string> version;

    bool operator==(const ToolRule&) const = default;
};

/// Règles durables par nom complet d'outil.
using ToolRuleMap = std::map<std::string, ToolRule, std::less<>>;

/// Politique effective de `tool` (risque `risk`) quand la politique par
/// niveau donne `policy` :
/// - « toujours refuser » refuse ;
/// - « toujours autoriser » ne fait que lever une confirmation (`Confirm` →
///   `Allow`), et seulement si le risque n'a pas été relevé depuis l'accord :
///   il ne contourne jamais un refus de la politique ;
/// - sans règle, `policy` est inchangée.
/// Les confirmations imposées ensuite (provenance d'un argument, examen de
/// l'outil) s'appliquent quand même : l'agent les vérifie après.
[[nodiscard]] Policy apply_rule(const ToolRule* rule, RiskLevel risk, Policy policy) noexcept;

/// Règles durables en vigueur, partagées par tous les agents d'Avenoir et
/// modifiables pendant qu'ils travaillent (une règle posée ou révoquée vaut
/// tout de suite, pour tous).
class ToolRules {
  public:
    ToolRules() = default;
    explicit ToolRules(ToolRuleMap rules) : rules_(std::move(rules)) {}

    /// Règle de `tool`, s'il en a une.
    [[nodiscard]] std::optional<ToolRule> find(std::string_view tool) const;
    void set(const std::string& tool, ToolRule rule);
    /// Vrai si une règle a été retirée.
    bool remove(std::string_view tool);
    void replace(ToolRuleMap rules);
    [[nodiscard]] ToolRuleMap all() const;

  private:
    mutable std::mutex mutex_;
    ToolRuleMap rules_;
};

/// Politique par niveau de risque, configurable dans `config.toml`.
struct PermissionPolicy {
    Policy read_only = Policy::Allow;
    Policy reversible = Policy::Allow;
    Policy sensitive = Policy::Confirm;
    /// Règles durables (`[permissions.tools]`), telles qu'écrites. Celles d'un
    /// outil de module d'une autre version ne sont pas mises en vigueur.
    ToolRuleMap tools;

    /// Politique effective pour un niveau de risque.
    ///
    /// Règle non négociable : une action `Sensitive` n'est **jamais**
    /// autorisée sans confirmation, même si la configuration dit `allow`.
    [[nodiscard]] Policy for_risk(RiskLevel risk) const;

    bool operator==(const PermissionPolicy&) const = default;
};

enum class Decision : std::uint8_t {
    Approved,
    Rejected,
};

/// Contexte d'une confirmation demandée par l'agent.
struct ConfirmContext {
    /// Vrai si l'utilisateur peut répondre « toujours autoriser » : la
    /// confirmation vient de la politique par niveau de risque, et non d'une
    /// protection imposée (provenance d'un argument, examen de l'outil :
    /// commandes shell, alertes), qui redemanderait de toute façon.
    bool can_always = false;
};

/// Demande à l'utilisateur de valider une action (terminal, popup, voix…).
class ConfirmationHandler {
  public:
    ConfirmationHandler() = default;
    ConfirmationHandler(const ConfirmationHandler&) = delete;
    ConfirmationHandler& operator=(const ConfirmationHandler&) = delete;
    ConfirmationHandler(ConfirmationHandler&&) = delete;
    ConfirmationHandler& operator=(ConfirmationHandler&&) = delete;
    virtual ~ConfirmationHandler() = default;

    [[nodiscard]] virtual Decision confirm(const ToolSpec& spec, const ToolCall& call) = 0;
    /// Confirmation d'une action de l'agent, qui dit si « toujours autoriser »
    /// peut être proposé. Par défaut : `confirm(spec, call)`.
    [[nodiscard]] virtual Decision confirm_action(const ToolSpec& spec, const ToolCall& call,
                                                  const ConfirmContext& /*context*/) {
        return confirm(spec, call);
    }
};

/// Refuse tout : pour le mode sans interface (tâches planifiées, tests).
class AutoReject final : public ConfirmationHandler {
  public:
    [[nodiscard]] Decision confirm(const ToolSpec& /*spec*/, const ToolCall& /*call*/) override {
        return Decision::Rejected;
    }
};

} // namespace avenoir::core
