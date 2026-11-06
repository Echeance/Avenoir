#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

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

/// Politique par niveau de risque, configurable dans `config.toml`.
struct PermissionPolicy {
    Policy read_only = Policy::Allow;
    Policy reversible = Policy::Allow;
    Policy sensitive = Policy::Confirm;

    /// Politique effective pour un niveau de risque.
    ///
    /// Règle non négociable : une action `Sensitive` n'est **jamais**
    /// autorisée sans confirmation, même si la configuration dit `allow`.
    [[nodiscard]] Policy for_risk(RiskLevel risk) const;
};

enum class Decision : std::uint8_t {
    Approved,
    Rejected,
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
};

/// Refuse tout : pour le mode sans interface (tâches planifiées, tests).
class AutoReject final : public ConfirmationHandler {
  public:
    [[nodiscard]] Decision confirm(const ToolSpec& /*spec*/, const ToolCall& /*call*/) override {
        return Decision::Rejected;
    }
};

} // namespace avenoir::core
