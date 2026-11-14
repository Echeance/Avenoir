#include "permissions.hpp"

#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace avenoir::daemon {

namespace {

std::string_view risk_label(core::RiskLevel risk) {
    switch (risk) {
    case core::RiskLevel::ReadOnly:
        return "lecture seule";
    case core::RiskLevel::Reversible:
        return "réversible";
    case core::RiskLevel::Sensitive:
        return "sensible";
    }
    return "sensible";
}

void audit(const std::string& message) {
    const auto logger = spdlog::get("audit");
    (logger != nullptr ? logger : spdlog::default_logger())->info("{}", message);
}

} // namespace

core::ToolRuleMap active_rules(const core::ToolRuleMap& written, const ToolVersions& versions,
                               std::vector<std::string>& warnings) {
    core::ToolRuleMap active;
    for (const auto& [tool, rule] : written) {
        if (rule.policy == core::Policy::Allow) {
            const auto module = versions.find(tool);
            if (module != versions.end() && rule.version != module->second) {
                warnings.push_back(std::format(
                    "« {} » : « toujours autoriser » avait été accordé à la version {} de son "
                    "module, pas à la {} : sa confirmation revient (/permissions)",
                    tool, rule.version.value_or("inconnue"), module->second));
                continue;
            }
        }
        active.emplace(tool, rule);
    }
    return active;
}

core::ToolRule make_rule(core::Policy policy, const core::ToolSpec& spec,
                         const ToolVersions& versions) {
    core::ToolRule rule{.policy = policy, .risk = spec.risk, .version = std::nullopt};
    if (const auto module = versions.find(spec.name); module != versions.end()) {
        rule.version = module->second;
    }
    return rule;
}

core::Result<Revocation> revoke_rules(const core::AppConfig& config, std::string_view target) {
    Revocation revocation{.updated = config, .tools = {}};
    auto& rules = revocation.updated.permissions.tools;
    if (target == "tout" || target == "toutes") {
        if (rules.empty()) {
            return core::make_error(core::ErrorCode::Config, "aucune règle durable à révoquer");
        }
        for (const auto& [tool, rule] : rules) {
            revocation.tools.push_back(tool);
        }
        rules.clear();
        return revocation;
    }
    const auto found = rules.find(target);
    if (found == rules.end()) {
        return core::make_error(
            core::ErrorCode::Config,
            std::format("« {} » n'a pas de règle durable (voir /permissions)", target));
    }
    revocation.tools.push_back(found->first);
    rules.erase(found);
    return revocation;
}

std::string describe_rule(const core::ToolRule& rule) {
    std::string out = rule.policy == core::Policy::Allow ? "toujours autorisé" : "toujours refusé";
    if (rule.policy == core::Policy::Allow) {
        out += std::format(" (jusqu'au risque {}", risk_label(rule.risk));
        if (rule.version) {
            out += ", version " + *rule.version;
        }
        out += ')';
    } else if (rule.version) {
        out += " (version " + *rule.version + ")";
    }
    return out;
}

PermissionKeeper::PermissionKeeper(core::AppConfig& config, Save save, ToolVersions versions)
    : config_(&config), save_(std::move(save)), versions_(std::move(versions)) {
    const std::scoped_lock lock(mutex_);
    refresh(warnings_);
    for (const auto& [tool, rule] : rules_->all()) {
        if (rule.policy == core::Policy::Allow && rule.risk == core::RiskLevel::Sensitive) {
            warnings_.push_back(std::format("action sensible toujours autorisée : « {} » "
                                            "(/revoquer {} pour la confirmer à nouveau)",
                                            tool, tool));
        }
    }
}

std::vector<std::string> PermissionKeeper::take_warnings() {
    const std::scoped_lock lock(mutex_);
    return std::exchange(warnings_, {});
}

void PermissionKeeper::refresh(std::vector<std::string>& warnings) {
    rules_->replace(active_rules(config_->permissions.tools, versions_, warnings));
}

core::Result<void> PermissionKeeper::remember(const core::ToolSpec& spec, core::Policy policy) {
    const std::scoped_lock lock(mutex_);
    core::AppConfig updated = *config_;
    const core::ToolRule rule = make_rule(policy, spec, versions_);
    updated.permissions.tools.insert_or_assign(spec.name, rule);
    if (auto saved = save_(updated); !saved) {
        return saved;
    }
    *config_ = std::move(updated);
    std::vector<std::string> ignored;
    refresh(ignored);
    audit(std::format("permissions : {} {}", spec.name, describe_rule(rule)));
    return {};
}

core::Result<std::vector<std::string>> PermissionKeeper::revoke(std::string_view target) {
    const std::scoped_lock lock(mutex_);
    auto revocation = revoke_rules(*config_, target);
    if (!revocation) {
        return std::unexpected(std::move(revocation.error()));
    }
    if (auto saved = save_(revocation->updated); !saved) {
        return std::unexpected(std::move(saved.error()));
    }
    *config_ = std::move(revocation->updated);
    std::vector<std::string> ignored;
    refresh(ignored);
    for (const auto& tool : revocation->tools) {
        audit(std::format("permissions : règle de {} révoquée (/revoquer)", tool));
    }
    return std::move(revocation->tools);
}

core::ToolRuleMap PermissionKeeper::written() const {
    const std::scoped_lock lock(mutex_);
    return config_->permissions.tools;
}

bool PermissionKeeper::inactive(std::string_view tool) const {
    const std::scoped_lock lock(mutex_);
    return config_->permissions.tools.contains(tool) && !rules_->find(tool).has_value();
}

} // namespace avenoir::daemon
