#include "avenoir/core/config.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <toml++/toml.hpp>

namespace avenoir::core {

namespace {

std::unexpected<Error> config_error(std::string_view where, std::string_view what) {
    return make_error(ErrorCode::Config, std::string(where) + " : " + std::string(what));
}

/// Première erreur parmi plusieurs résultats, ou rien si tous ont réussi.
template <typename... Results>
std::optional<Error> first_error(const Results&... results) {
    std::optional<Error> error;
    const auto keep_first = [&error](const auto& result) {
        if (!error && !result) {
            error = result.error();
        }
    };
    (keep_first(results), ...);
    return error;
}

std::string key_path(std::string_view section, std::string_view key) {
    return std::string(section) + "." + std::string(key);
}

/// Valeur facultative de type `T` ; erreur si la clé existe avec un autre type.
template <typename T>
Result<std::optional<T>> optional_value(const toml::table& table, std::string_view section,
                                        std::string_view key) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::optional<T>{};
    }
    if (std::optional<T> value = node->value<T>()) {
        return value;
    }
    return config_error(key_path(section, key), "type invalide");
}

/// Valeur obligatoire de type `T`.
template <typename T>
Result<T> required_value(const toml::table& table, std::string_view section, std::string_view key) {
    auto value = optional_value<T>(table, section, key);
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    if (!value->has_value()) {
        return config_error(key_path(section, key), "clé obligatoire absente");
    }
    return std::move(**value);
}

/// Entier facultatif compris entre `min` et `max`.
Result<std::optional<std::int64_t>> optional_integer(const toml::table& table,
                                                     std::string_view section, std::string_view key,
                                                     std::int64_t min, std::int64_t max) {
    auto value = optional_value<std::int64_t>(table, section, key);
    if (value && value->has_value() && (**value < min || **value > max)) {
        return config_error(key_path(section, key), "valeur hors limites (" + std::to_string(min) +
                                                        " à " + std::to_string(max) + ")");
    }
    return value;
}

/// Sous-table facultative (`[section]`) ; erreur si la clé n'est pas une table.
Result<const toml::table*> optional_table(const toml::table& root, std::string_view key) {
    const toml::node* node = root.get(key);
    if (node == nullptr) {
        return nullptr;
    }
    if (const auto* table = node->as_table()) {
        return table;
    }
    return config_error(key, "une table est attendue");
}

// ─── [llm] ──────────────────────────────────────────────────────────────────

Result<LlmConfig> parse_llm(const toml::table& llm, std::string_view section) {
    auto provider = required_value<std::string>(llm, section, "provider");
    if (!provider) {
        return std::unexpected(std::move(provider.error()));
    }

    if (*provider == "openai_compatible") {
        OpenAiCompatibleConfig config;
        auto base_url = required_value<std::string>(llm, section, "base_url");
        auto model = required_value<std::string>(llm, section, "model");
        auto key_env = optional_value<std::string>(llm, section, "api_key_env");
        auto temperature = optional_value<double>(llm, section, "temperature");
        auto effort = optional_value<std::string>(llm, section, "reasoning_effort");
        if (auto error = first_error(base_url, model, key_env, temperature, effort)) {
            return std::unexpected(std::move(*error));
        }
        if (*effort && **effort != "none" && **effort != "low" && **effort != "medium" &&
            **effort != "high") {
            return config_error(key_path(section, "reasoning_effort"),
                                "none, low, medium ou high attendu");
        }
        config.base_url = std::move(*base_url);
        config.model = std::move(*model);
        config.api_key_env = std::move(*key_env);
        config.temperature = *temperature;
        config.reasoning_effort = std::move(*effort);
        return config;
    }

    if (*provider == "anthropic") {
        AnthropicConfig config;
        auto model = required_value<std::string>(llm, section, "model");
        auto key_env = optional_value<std::string>(llm, section, "api_key_env");
        auto max_tokens = optional_integer(llm, section, "max_tokens", 1,
                                           std::numeric_limits<std::uint32_t>::max());
        auto base_url = optional_value<std::string>(llm, section, "base_url");
        if (auto error = first_error(model, key_env, max_tokens, base_url)) {
            return std::unexpected(std::move(*error));
        }
        config.model = std::move(*model);
        if (key_env->has_value()) {
            config.api_key_env = std::move(**key_env);
        }
        if (max_tokens->has_value()) {
            config.max_tokens = static_cast<std::uint32_t>(**max_tokens);
        }
        config.base_url = std::move(*base_url);
        return config;
    }

    if (*provider == "claude_code") {
        ClaudeCodeConfig config;
        auto model = optional_value<std::string>(llm, section, "model");
        auto command = optional_value<std::string>(llm, section, "command");
        auto timeout = optional_integer(llm, section, "timeout_secs", 1, 3600);
        if (auto error = first_error(model, command, timeout)) {
            return std::unexpected(std::move(*error));
        }
        config.model = std::move(*model);
        if (command->has_value()) {
            config.command = std::move(**command);
        }
        if (timeout->has_value()) {
            config.timeout_secs = static_cast<std::uint32_t>(**timeout);
        }
        return config;
    }

    return config_error(key_path(section, "provider"),
                        "fournisseur inconnu « " + *provider +
                            " » (openai_compatible, anthropic ou claude_code)");
}

// ─── [agent] ────────────────────────────────────────────────────────────────

Result<AgentConfig> parse_agent(const toml::table& agent) {
    constexpr std::string_view kSection = "agent";
    AgentConfig config;
    auto max_steps = optional_integer(agent, kSection, "max_steps", 1, 100);
    auto prompt = optional_value<std::string>(agent, kSection, "system_prompt");
    auto fast_path = optional_value<bool>(agent, kSection, "fast_path");
    auto unload_on_switch = optional_value<bool>(agent, kSection, "unload_on_switch");
    if (auto error = first_error(max_steps, prompt, fast_path, unload_on_switch)) {
        return std::unexpected(std::move(*error));
    }
    if (max_steps->has_value()) {
        config.max_steps = static_cast<std::size_t>(**max_steps);
    }
    config.system_prompt = std::move(*prompt);
    config.fast_path = fast_path->value_or(config.fast_path);
    config.unload_on_switch = unload_on_switch->value_or(config.unload_on_switch);
    return config;
}

// ─── [permissions] ──────────────────────────────────────────────────────────

Result<PermissionPolicy> parse_permissions(const toml::table& permissions) {
    constexpr std::string_view kSection = "permissions";
    PermissionPolicy policy;
    const std::array<std::pair<std::string_view, Policy*>, 3> fields{{
        {"read_only", &policy.read_only},
        {"reversible", &policy.reversible},
        {"sensitive", &policy.sensitive},
    }};
    for (const auto& [key, target] : fields) {
        auto text = optional_value<std::string>(permissions, kSection, key);
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        if (!text->has_value()) {
            continue;
        }
        const auto parsed = policy_from_string(**text);
        if (!parsed) {
            return config_error(key_path(kSection, key),
                                "« " + **text + " » (allow, confirm ou deny attendu)");
        }
        *target = *parsed;
    }
    return policy;
}

// ─── [modules.<id>] ─────────────────────────────────────────────────────────

Result<ModuleSettings> parse_module_settings(const toml::table& module,
                                             const std::string& section) {
    ModuleSettings settings;
    for (const auto& [key, value] : module) {
        if (key.str() != "enabled" && key.str() != "risk") {
            return config_error(key_path(section, key.str()),
                                "clé inconnue (enabled ou risk attendu)");
        }
    }
    auto enabled = optional_value<bool>(module, section, "enabled");
    if (!enabled) {
        return std::unexpected(std::move(enabled.error()));
    }
    settings.enabled = *enabled;

    const toml::node* risk = module.get("risk");
    if (risk == nullptr) {
        return settings;
    }
    const auto* table = risk->as_table();
    if (table == nullptr) {
        return config_error(key_path(section, "risk"),
                            "table attendue (risk = { outil = \"sensitive\" })");
    }
    for (const auto& [tool, level] : *table) {
        const std::string where = key_path(key_path(section, "risk"), tool.str());
        if (!is_valid_tool_name(tool.str())) {
            return config_error(where, "nom d'outil invalide");
        }
        const auto text = level.value<std::string>();
        const auto parsed = text ? risk_level_from_string(*text) : std::nullopt;
        if (!parsed) {
            return config_error(where, "read_only, reversible ou sensitive attendu");
        }
        settings.risk.emplace(std::string(tool.str()), *parsed);
    }
    return settings;
}

/// Nom d'un modèle : minuscules ASCII, chiffres, « - » et « _ » (32 au plus).
bool is_valid_model_name(std::string_view name) {
    return !name.empty() && name.size() <= 32 && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

/// Clé `local` (booléen facultatif), réservée à `openai_compatible`.
Result<std::optional<bool>> parse_local(const toml::table& table, std::string_view section,
                                        const LlmConfig& llm) {
    auto local = optional_value<bool>(table, section, "local");
    if (local && local->has_value() && !std::holds_alternative<OpenAiCompatibleConfig>(llm)) {
        return config_error(key_path(section, "local"),
                            "réservé à openai_compatible (les autres fournisseurs sont distants)");
    }
    return local;
}

/// `[models.<nom>]` : les modèles, dans l'ordre du fichier (le premier sert
/// par défaut).
Result<std::vector<NamedLlm>> parse_models(const toml::table& root) {
    std::vector<NamedLlm> models;
    std::vector<toml::source_index> lines; // ligne de chaque modèle dans le fichier
    auto table = optional_table(root, "models");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    if (*table == nullptr) {
        return models;
    }
    for (const auto& [key, node] : **table) {
        const std::string name(key.str());
        const std::string section = "models." + name;
        const auto* model = node.as_table();
        if (model == nullptr) {
            return config_error(section, "une table est attendue ([" + section + "])");
        }
        if (!is_valid_model_name(name)) {
            return config_error(section, "nom invalide (minuscules, chiffres, - et _)");
        }
        auto llm = parse_llm(*model, section);
        if (!llm) {
            return std::unexpected(std::move(llm.error()));
        }
        auto local = parse_local(*model, section, *llm);
        if (!local) {
            return std::unexpected(std::move(local.error()));
        }
        models.push_back(NamedLlm{.name = name, .config = std::move(*llm), .local = *local});
        lines.push_back(node.source().begin.line);
    }
    // toml++ range les clés par ordre alphabétique : on rétablit celui du
    // fichier, qui désigne le modèle par défaut.
    std::vector<std::size_t> order(models.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order,
                      [&lines](std::size_t a, std::size_t b) { return lines[a] < lines[b]; });
    std::vector<NamedLlm> sorted;
    sorted.reserve(models.size());
    for (const std::size_t i : order) {
        sorted.push_back(std::move(models[i]));
    }
    return sorted;
}

Result<std::map<std::string, ModuleSettings, std::less<>>> parse_modules(const toml::table& root) {
    std::map<std::string, ModuleSettings, std::less<>> modules;
    auto table = optional_table(root, "modules");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    if (*table == nullptr) {
        return modules;
    }
    for (const auto& [id, node] : **table) {
        const std::string section = "modules." + std::string(id.str());
        const auto* module = node.as_table();
        if (module == nullptr) {
            return config_error(section, "une table est attendue ([" + section + "])");
        }
        auto settings = parse_module_settings(*module, section);
        if (!settings) {
            return std::unexpected(std::move(settings.error()));
        }
        modules.emplace(std::string(id.str()), std::move(*settings));
    }
    return modules;
}

} // namespace

Result<AppConfig> parse_config(std::string_view toml_text) {
    toml::table root;
    try {
        root = toml::parse(toml_text);
    } catch (const toml::parse_error& e) {
        const auto& begin = e.source().begin;
        return make_error(ErrorCode::Config, "TOML illisible (ligne " + std::to_string(begin.line) +
                                                 ", colonne " + std::to_string(begin.column) +
                                                 ") : " + std::string(e.description()));
    }

    if (root.contains("llm")) {
        return config_error("llm", "section remplacée par [models.<nom>] : un modèle par section, "
                                   "le premier sert par défaut (voir config.example.toml)");
    }
    auto agent_table = optional_table(root, "agent");
    auto permissions_table = optional_table(root, "permissions");
    if (auto error = first_error(agent_table, permissions_table)) {
        return std::unexpected(std::move(*error));
    }
    AppConfig config;
    auto models = parse_models(root);
    if (!models) {
        return std::unexpected(std::move(models.error()));
    }
    config.models = std::move(*models);

    if (*agent_table != nullptr) {
        auto agent = parse_agent(**agent_table);
        if (!agent) {
            return std::unexpected(std::move(agent.error()));
        }
        config.agent = std::move(*agent);
    }
    if (*permissions_table != nullptr) {
        auto permissions = parse_permissions(**permissions_table);
        if (!permissions) {
            return std::unexpected(std::move(permissions.error()));
        }
        config.permissions = *permissions;
    }
    auto modules = parse_modules(root);
    if (!modules) {
        return std::unexpected(std::move(modules.error()));
    }
    config.modules = std::move(*modules);
    return config;
}

Result<AppConfig> load_config(const std::filesystem::path& path) {
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return make_error(ErrorCode::Config, "lecture impossible : " + path.string());
    }
    std::ostringstream content;
    content << file.rdbuf();
    return parse_config(content.str());
}

} // namespace avenoir::core
