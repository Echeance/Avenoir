#include "avenoir/core/permission.hpp"

#include <optional>
#include <string_view>

#include <spdlog/spdlog.h>

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
        if (sensitive == Policy::Allow) {
            spdlog::warn("`sensitive = \"allow\"` ignoré : confirmation imposée");
            return Policy::Confirm;
        }
        return sensitive;
    }
    return Policy::Deny;
}

} // namespace avenoir::core
