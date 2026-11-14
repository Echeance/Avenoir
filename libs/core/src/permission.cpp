#include "avenoir/core/permission.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace avenoir::core {

std::string_view to_string(Policy policy) noexcept {
    switch (policy) {
    case Policy::Allow:
        return "allow";
    case Policy::Confirm:
        return "confirm";
    case Policy::Deny:
        return "deny";
    }
    return "deny";
}

std::optional<Policy> policy_from_string(std::string_view text) noexcept {
    if (text == "allow") {
        return Policy::Allow;
    }
    if (text == "confirm") {
        return Policy::Confirm;
    }
    if (text == "deny") {
        return Policy::Deny;
    }
    return std::nullopt;
}

Policy PermissionPolicy::for_risk(RiskLevel risk) const {
    switch (risk) {
    case RiskLevel::ReadOnly:
        return read_only;
    case RiskLevel::Reversible:
        return reversible;
    case RiskLevel::Sensitive:
        // « allow » n'arrive ici qu'après l'avertissement et « Je sais ce que
        // je fais » (réglages scellés, décision 59) ; la provenance et les
        // outils à confirmation obligatoire (run_command) confirment quand même.
        return sensitive;
    }
    return Policy::Deny;
}

Policy apply_rule(const ToolRule* rule, RiskLevel risk, Policy policy) noexcept {
    if (rule == nullptr) {
        return policy;
    }
    if (rule->policy == Policy::Deny) {
        return Policy::Deny;
    }
    if (rule->policy == Policy::Allow && policy == Policy::Confirm && risk <= rule->risk) {
        return Policy::Allow;
    }
    return policy;
}

std::optional<ToolRule> ToolRules::find(std::string_view tool) const {
    const std::scoped_lock lock(mutex_);
    const auto found = rules_.find(tool);
    if (found == rules_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void ToolRules::set(const std::string& tool, ToolRule rule) {
    const std::scoped_lock lock(mutex_);
    rules_.insert_or_assign(tool, std::move(rule));
}

bool ToolRules::remove(std::string_view tool) {
    const std::scoped_lock lock(mutex_);
    const auto found = rules_.find(tool);
    if (found == rules_.end()) {
        return false;
    }
    rules_.erase(found);
    return true;
}

void ToolRules::replace(ToolRuleMap rules) {
    const std::scoped_lock lock(mutex_);
    rules_ = std::move(rules);
}

ToolRuleMap ToolRules::all() const {
    const std::scoped_lock lock(mutex_);
    return rules_;
}

} // namespace avenoir::core
