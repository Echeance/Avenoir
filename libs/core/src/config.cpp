#include "avenoir/core/config.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

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

Result<LlmConfig> parse_llm(const toml::table& llm) {
    constexpr std::string_view kSection = "llm";
    auto provider = required_value<std::string>(llm, kSection, "provider");
    if (!provider) {
        return std::unexpected(std::move(provider.error()));
    }

    if (*provider == "openai_compatible") {
        OpenAiCompatibleConfig config;
        auto base_url = required_value<std::string>(llm, kSection, "base_url");
        auto model = required_value<std::string>(llm, kSection, "model");
        auto key_env = optional_value<std::string>(llm, kSection, "api_key_env");
        auto temperature = optional_value<double>(llm, kSection, "temperature");
        if (auto error = first_error(base_url, model, key_env, temperature)) {
            return std::unexpected(std::move(*error));
        }
        config.base_url = std::move(*base_url);
        config.model = std::move(*model);
        config.api_key_env = std::move(*key_env);
        config.temperature = *temperature;
        return config;
    }

    if (*provider == "anthropic") {
        AnthropicConfig config;
        auto model = required_value<std::string>(llm, kSection, "model");
        auto key_env = optional_value<std::string>(llm, kSection, "api_key_env");
        auto max_tokens = optional_integer(llm, kSection, "max_tokens", 1,
                                           std::numeric_limits<std::uint32_t>::max());
        auto base_url = optional_value<std::string>(llm, kSection, "base_url");
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
        auto model = optional_value<std::string>(llm, kSection, "model");
        auto command = optional_value<std::string>(llm, kSection, "command");
        auto timeout = optional_integer(llm, kSection, "timeout_secs", 1, 3600);
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

    return config_error("llm.provider", "fournisseur inconnu « " + *provider +
                                            " » (openai_compatible, anthropic ou claude_code)");
}

// ─── [agent] ────────────────────────────────────────────────────────────────

Result<AgentConfig> parse_agent(const toml::table& agent) {
    constexpr std::string_view kSection = "agent";
    AgentConfig config;
    auto max_steps = optional_integer(agent, kSection, "max_steps", 1, 100);
    auto prompt = optional_value<std::string>(agent, kSection, "system_prompt");
    auto fast_path = optional_value<bool>(agent, kSection, "fast_path");
    if (auto error = first_error(max_steps, prompt, fast_path)) {
        return std::unexpected(std::move(*error));
    }
    if (max_steps->has_value()) {
        config.max_steps = static_cast<std::size_t>(**max_steps);
    }
    config.system_prompt = std::move(*prompt);
    config.fast_path = fast_path->value_or(config.fast_path);
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

// ─── [[mcp_servers]] ────────────────────────────────────────────────────────

Result<McpServerConfig> parse_mcp_server(const toml::table& server, std::size_t index) {
    const std::string section = "mcp_servers[" + std::to_string(index) + "]";
    McpServerConfig config;

    auto name = required_value<std::string>(server, section, "name");
    auto command = required_value<std::string>(server, section, "command");
    auto risk = optional_value<std::string>(server, section, "risk");
    auto enabled = optional_value<bool>(server, section, "enabled");
    if (auto error = first_error(name, command, risk, enabled)) {
        return std::unexpected(std::move(*error));
    }
    config.name = std::move(*name);
    config.command = std::move(*command);
    config.enabled = enabled->value_or(true);
    if (risk->has_value()) {
        const auto parsed = risk_level_from_string(**risk);
        if (!parsed) {
            return config_error(key_path(section, "risk"),
                                "« " + **risk + " » (read_only, reversible ou sensitive attendu)");
        }
        config.risk = *parsed;
    }

    if (const toml::node* args = server.get("args")) {
        const auto* array = args->as_array();
        if (array == nullptr) {
            return config_error(key_path(section, "args"), "tableau de chaînes attendu");
        }
        for (const auto& item : *array) {
            auto arg = item.value<std::string>();
            if (!arg) {
                return config_error(key_path(section, "args"), "tableau de chaînes attendu");
            }
            config.args.push_back(std::move(*arg));
        }
    }

    if (const toml::node* env = server.get("env")) {
        const auto* table = env->as_table();
        if (table == nullptr) {
            return config_error(key_path(section, "env"), "table de chaînes attendue");
        }
        for (const auto& [key, value] : *table) {
            auto text = value.value<std::string>();
            if (!text) {
                return config_error(key_path(section, "env"), "table de chaînes attendue");
            }
            config.env.emplace(std::string(key.str()), std::move(*text));
        }
    }
    return config;
}

Result<std::vector<McpServerConfig>> parse_mcp_servers(const toml::table& root) {
    std::vector<McpServerConfig> servers;
    const toml::node* node = root.get("mcp_servers");
    if (node == nullptr) {
        return servers;
    }
    const auto* array = node->as_array();
    if (array == nullptr) {
        return config_error("mcp_servers", "tableau de tables attendu ([[mcp_servers]])");
    }
    for (std::size_t i = 0; i < array->size(); ++i) {
        const auto* table = array->get(i)->as_table();
        if (table == nullptr) {
            return config_error("mcp_servers", "tableau de tables attendu ([[mcp_servers]])");
        }
        auto server = parse_mcp_server(*table, i);
        if (!server) {
            return std::unexpected(std::move(server.error()));
        }
        servers.push_back(std::move(*server));
    }
    return servers;
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

    auto llm_table = optional_table(root, "llm");
    auto agent_table = optional_table(root, "agent");
    auto permissions_table = optional_table(root, "permissions");
    if (auto error = first_error(llm_table, agent_table, permissions_table)) {
        return std::unexpected(std::move(*error));
    }
    if (*llm_table == nullptr) {
        return config_error("llm", "section obligatoire absente");
    }

    auto llm = parse_llm(**llm_table);
    if (!llm) {
        return std::unexpected(std::move(llm.error()));
    }
    AppConfig config{.llm = std::move(*llm), .agent = {}, .permissions = {}, .mcp_servers = {}};

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
    auto servers = parse_mcp_servers(root);
    if (!servers) {
        return std::unexpected(std::move(servers.error()));
    }
    config.mcp_servers = std::move(*servers);
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
