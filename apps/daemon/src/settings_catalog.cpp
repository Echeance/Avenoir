#include "settings_catalog.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/permission.hpp"

namespace avenoir::daemon {

namespace {

constexpr std::string_view kBooleans = "oui | non";
constexpr std::string_view kPolicies = "autoriser | confirmer | refuser";

std::unexpected<core::Error> setting_error(std::string message) {
    return core::make_error(core::ErrorCode::Config, std::move(message));
}

std::string_view yes_no(bool value) {
    return value ? "oui" : "non";
}

std::optional<bool> parse_bool(std::string_view text) {
    if (text == "oui" || text == "o" || text == "true" || text == "on" || text == "1") {
        return true;
    }
    if (text == "non" || text == "n" || text == "false" || text == "off" || text == "0") {
        return false;
    }
    return std::nullopt;
}

std::string_view policy_label(core::Policy policy) {
    switch (policy) {
    case core::Policy::Allow:
        return "autoriser";
    case core::Policy::Confirm:
        return "confirmer";
    case core::Policy::Deny:
        return "refuser";
    }
    return "confirmer";
}

std::optional<core::Policy> parse_policy(std::string_view text) {
    if (text == "autoriser") {
        return core::Policy::Allow;
    }
    if (text == "confirmer") {
        return core::Policy::Confirm;
    }
    if (text == "refuser") {
        return core::Policy::Deny;
    }
    return core::policy_from_string(text); // allow, confirm, deny
}

/// Plus la politique laisse passer, plus le rang est haut.
int permissiveness(core::Policy policy) {
    switch (policy) {
    case core::Policy::Deny:
        return 0;
    case core::Policy::Confirm:
        return 1;
    case core::Policy::Allow:
        return 2;
    }
    return 1;
}

std::optional<std::int64_t> parse_integer(std::string_view text, std::int64_t min,
                                          std::int64_t max) {
    std::int64_t value = 0;
    const auto [end, error] =
        std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value);
    if (error != std::errc{} || end != std::to_address(text.end()) || value < min || value > max) {
        return std::nullopt;
    }
    return value;
}

bool module_enabled(const core::AppConfig& config, const KnownModule& module) {
    const auto found = config.modules.find(module.id);
    if (found == config.modules.end()) {
        return module.bundled;
    }
    return found->second.enabled.value_or(module.bundled);
}

const KnownModule* find_module(const std::vector<KnownModule>& modules, std::string_view id) {
    const auto found = std::ranges::find(modules, id, &KnownModule::id);
    return found == modules.end() ? nullptr : &*found;
}

/// `modules.<id>.enabled` → `<id>` ; rien si la clé n'a pas cette forme.
std::optional<std::string_view> module_key(std::string_view key) {
    constexpr std::string_view kPrefix = "modules.";
    constexpr std::string_view kSuffix = ".enabled";
    if (!key.starts_with(kPrefix) || !key.ends_with(kSuffix) ||
        key.size() <= kPrefix.size() + kSuffix.size()) {
        return std::nullopt;
    }
    return key.substr(kPrefix.size(), key.size() - kPrefix.size() - kSuffix.size());
}

/// La politique `name` de `policy`, par référence.
core::Policy* policy_field(core::PermissionPolicy& policy, std::string_view name) {
    if (name == "permissions.read_only") {
        return &policy.read_only;
    }
    if (name == "permissions.reversible") {
        return &policy.reversible;
    }
    if (name == "permissions.sensitive") {
        return &policy.sensitive;
    }
    return nullptr;
}

/// Booléen `field` (dans `change.updated`) mis à `value`.
core::Result<void> change_bool(SettingChange& change, bool& field, std::string_view value,
                               std::optional<std::string> weakening_when_off,
                               bool strengthens_when_on) {
    const auto parsed = parse_bool(value);
    if (!parsed) {
        return setting_error(std::format("{} : « oui » ou « non » attendu", change.key));
    }
    change.before = yes_no(field);
    change.after = yes_no(*parsed);
    if (field && !*parsed) {
        change.weakening = std::move(weakening_when_off);
    }
    change.strengthens = strengthens_when_on && !field && *parsed;
    field = *parsed;
    return {};
}

} // namespace

std::vector<SettingLine> list_settings(const core::AppConfig& config,
                                       const std::vector<KnownModule>& modules) {
    const auto& agent = config.agent;
    const auto& permissions = config.permissions;
    std::vector<SettingLine> lines{
        {.key = "agent.local_only",
         .value = std::string(yes_no(agent.local_only)),
         .accepted = std::string(kBooleans),
         .description = "local uniquement : aucun modèle distant, la conversation ne quitte "
                        "pas votre machine ou votre réseau",
         .security = true},
        {.key = "agent.verify_replies",
         .value = std::string(yes_no(agent.verify_replies)),
         .accepted = std::string(kBooleans),
         .description = "vérifier qu'une action annoncée par le modèle a bien été faite",
         .security = true},
        {.key = "agent.fast_path",
         .value = std::string(yes_no(agent.fast_path)),
         .accepted = std::string(kBooleans),
         .description = "voie rapide : commandes simples traitées sans LLM",
         .security = false},
        {.key = "agent.max_steps",
         .value = std::to_string(agent.max_steps),
         .accepted = "1 à 100",
         .description = "allers-retours LLM ↔ outils au plus par requête",
         .security = false},
        {.key = "agent.unload_on_switch",
         .value = std::string(yes_no(agent.unload_on_switch)),
         .accepted = std::string(kBooleans),
         .description = "décharger l'ancien modèle local en changeant de modèle",
         .security = false},
        {.key = "permissions.read_only",
         .value = std::string(policy_label(permissions.read_only)),
         .accepted = std::string(kPolicies),
         .description = "actions en lecture seule",
         .security = true},
        {.key = "permissions.reversible",
         .value = std::string(policy_label(permissions.reversible)),
         .accepted = std::string(kPolicies),
         .description = "actions réversibles (son, lancer une application…)",
         .security = true},
        {.key = "permissions.sensitive",
         .value = std::string(policy_label(permissions.sensitive)),
         .accepted = "confirmer | refuser",
         .description = "actions sensibles (jamais sans confirmation)",
         .security = true},
        {.key = "web.local_searxng",
         .value = std::string(yes_no(config.web.local_searxng)),
         .accepted = std::string(kBooleans),
         .description = "lancer SearXNG en local (podman ou docker) pour chercher sur le web",
         .security = false},
        {.key = "web.searxng_idle_minutes",
         .value = std::to_string(config.web.searxng_idle_minutes),
         .accepted = "1 à 1440",
         .description = "arrêt de l'instance SearXNG locale après ce temps sans recherche",
         .security = false},
        {.key = "web.searxng_url",
         .value = config.web.searxng_url.value_or("aucune"),
         .accepted = "http(s)://… | aucune",
         .description = "votre propre instance SearXNG (les recherches y sont envoyées)",
         .security = true},
    };
    for (const auto& module : modules) {
        lines.push_back({.key = std::format("modules.{}.enabled", module.id),
                         .value = std::string(yes_no(module_enabled(config, module))),
                         .accepted = std::string(kBooleans),
                         .description = module.bundled ? "module fourni avec Avenoir"
                                                       : "module installé par vous",
                         .security = !module.bundled});
    }
    return lines;
}

core::Result<SettingChange> change_setting(const core::AppConfig& config, std::string_view key,
                                           std::string_view value,
                                           const std::vector<KnownModule>& modules) {
    SettingChange change{.updated = config,
                         .key = std::string(key),
                         .before = {},
                         .after = {},
                         .weakening = std::nullopt,
                         .strengthens = false};
    auto& agent = change.updated.agent;
    core::Result<void> done =
        setting_error(std::format("réglage inconnu : « {} » (voir /reglages)", key));

    if (key == "agent.local_only") {
        done = change_bool(change, agent.local_only, value,
                           "Les modèles distants redeviendront utilisables : vos conversations "
                           "pourront quitter votre machine et votre réseau.",
                           true);
    } else if (key == "agent.verify_replies") {
        done = change_bool(change, agent.verify_replies, value,
                           "Une action annoncée par le modèle sans avoir été faite ne sera plus "
                           "détectée.",
                           true);
    } else if (key == "agent.fast_path") {
        done = change_bool(change, agent.fast_path, value, std::nullopt, false);
    } else if (key == "agent.unload_on_switch") {
        done = change_bool(change, agent.unload_on_switch, value, std::nullopt, false);
    } else if (key == "web.local_searxng") {
        done = change_bool(change, change.updated.web.local_searxng, value, std::nullopt, false);
    } else if (key == "agent.max_steps") {
        const auto parsed = parse_integer(value, 1, 100);
        if (!parsed) {
            return setting_error("agent.max_steps : nombre de 1 à 100 attendu");
        }
        change.before = std::to_string(agent.max_steps);
        change.after = std::to_string(*parsed);
        agent.max_steps = static_cast<std::size_t>(*parsed);
        done = {};
    } else if (key == "web.searxng_idle_minutes") {
        const auto parsed = parse_integer(value, 1, 1440);
        if (!parsed) {
            return setting_error("web.searxng_idle_minutes : nombre de 1 à 1440 attendu");
        }
        change.before = std::to_string(change.updated.web.searxng_idle_minutes);
        change.after = std::to_string(*parsed);
        change.updated.web.searxng_idle_minutes = static_cast<int>(*parsed);
        done = {};
    } else if (key == "web.searxng_url") {
        auto& url = change.updated.web.searxng_url;
        change.before = url.value_or("aucune");
        if (value == "aucune" || value == "aucun") {
            url.reset();
            change.after = "aucune";
        } else {
            url = std::string(value);
            change.after = *url;
            if (change.before != change.after) {
                change.weakening = "Vos recherches web seront envoyées à " + change.after +
                                   " : ne le faites que pour une instance à vous ou de confiance.";
            }
        }
        done = {};
    } else if (auto* field = policy_field(change.updated.permissions, key)) {
        const auto parsed = parse_policy(value);
        if (!parsed) {
            return setting_error(std::format("{} : autoriser, confirmer ou refuser attendu", key));
        }
        if (key == "permissions.sensitive" && *parsed == core::Policy::Allow) {
            return setting_error("permissions.sensitive : une action sensible demande toujours "
                                 "confirmation (« autoriser » est refusé)");
        }
        change.before = policy_label(*field);
        change.after = policy_label(*parsed);
        if (permissiveness(*parsed) > permissiveness(*field)) {
            change.weakening = *parsed == core::Policy::Allow
                                   ? "Ces actions s'exécuteront sans vous demander de confirmation."
                                   : "Ces actions, aujourd'hui refusées, deviendront possibles "
                                     "(avec confirmation).";
        }
        change.strengthens = permissiveness(*parsed) < permissiveness(*field);
        *field = *parsed;
        done = {};
    } else if (const auto id = module_key(key)) {
        const auto* module = find_module(modules, *id);
        if (module == nullptr) {
            return setting_error(std::format("module inconnu : « {} » (voir /modules)", *id));
        }
        const auto parsed = parse_bool(value);
        if (!parsed) {
            return setting_error(std::format("{} : « oui » ou « non » attendu", key));
        }
        const bool before = module_enabled(config, *module);
        change.before = yes_no(before);
        change.after = yes_no(*parsed);
        if (!before && *parsed && !module->bundled) {
            change.weakening = "Ce module, installé par vous et non fourni avec Avenoir, sera "
                               "lancé et ses outils proposés au modèle (avec leurs contrôles).";
        }
        change.strengthens = before && !*parsed && !module->bundled;
        change.updated.modules[std::string(*id)].enabled = parsed;
        done = {};
    }

    if (!done) {
        return std::unexpected(std::move(done.error()));
    }
    // Mêmes règles que le fichier : ce qu'Avenoir écrit doit pouvoir être relu.
    if (auto reread = core::parse_config(core::format_config(change.updated)); !reread) {
        return std::unexpected(std::move(reread.error()));
    }
    return change;
}

} // namespace avenoir::daemon
