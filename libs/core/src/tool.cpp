#include "avenoir/core/tool.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avenoir::core {

std::string_view to_string(RiskLevel risk) noexcept {
    switch (risk) {
    case RiskLevel::ReadOnly:
        return "read_only";
    case RiskLevel::Reversible:
        return "reversible";
    case RiskLevel::Sensitive:
        return "sensitive";
    }
    return "sensitive";
}

std::optional<RiskLevel> risk_level_from_string(std::string_view text) noexcept {
    if (text == "read_only") {
        return RiskLevel::ReadOnly;
    }
    if (text == "reversible") {
        return RiskLevel::Reversible;
    }
    if (text == "sensitive") {
        return RiskLevel::Sensitive;
    }
    return std::nullopt;
}

std::string describe(const ToolError& error) {
    switch (error.kind) {
    case ToolErrorKind::InvalidArguments:
        return "arguments invalides : " + error.message;
    case ToolErrorKind::Execution:
        return "échec de l'exécution : " + error.message;
    case ToolErrorKind::Unsupported:
        return "action non supportée sur cette plateforme";
    }
    return error.message;
}

bool is_valid_tool_name(std::string_view name) noexcept {
    const auto allowed = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    };
    return !name.empty() && name.size() <= 64 && std::ranges::all_of(name, allowed);
}

Result<void> ToolRegistry::add(std::shared_ptr<const Tool> tool) {
    if (tool == nullptr) {
        return make_error(ErrorCode::Config, "outil nul");
    }
    std::string name = tool->spec().name;
    if (!is_valid_tool_name(name)) {
        return make_error(ErrorCode::Config, "nom d'outil invalide : « " + name + " »");
    }
    if (tools_.contains(name)) {
        return make_error(ErrorCode::Config, "outil déjà enregistré : « " + name + " »");
    }
    tools_.emplace(std::move(name), std::move(tool));
    return {};
}

std::shared_ptr<const Tool> ToolRegistry::find(std::string_view name) const {
    const auto it = tools_.find(name);
    return it == tools_.end() ? nullptr : it->second;
}

std::vector<ToolSpec> ToolRegistry::specs() const {
    std::vector<ToolSpec> out;
    out.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        out.push_back(tool->spec());
    }
    return out;
}

} // namespace avenoir::core
