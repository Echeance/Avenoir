// Écriture de la configuration : emplacement standard, texte TOML et
// enregistrement sûr du fichier (création guidée, puis le démon, seul
// rédacteur des réglages au jalon 3).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"

namespace avenoir::core {

namespace {

/// Chaîne TOML de base (entre guillemets), caractères de contrôle échappés.
std::string toml_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (byte < 0x20U || byte == 0x7FU) {
            out += std::format("\\u{:04X}", static_cast<unsigned>(byte));
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

/// Clé TOML : nue si possible, sinon entre guillemets.
std::string key(std::string_view name) {
    const bool bare = !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
    return bare ? std::string(name) : toml_string(name);
}

/// Nombre à virgule TOML : toujours reconnu comme tel (`1.0`, pas `1`).
std::string floating(double value) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-inf" : "inf";
    }
    std::string text = std::format("{}", value);
    if (text.find_first_of(".e") == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string_view boolean(bool value) {
    return value ? "true" : "false";
}

void line(std::string& out, std::string_view name, std::string_view value) {
    out += std::format("{} = {}\n", name, value);
}

void format_model(std::string& out, const NamedLlm& model) {
    out += std::format("\n[models.{}]\n", key(model.name));
    if (const auto* openai = std::get_if<OpenAiCompatibleConfig>(&model.config)) {
        line(out, "provider", toml_string("openai_compatible"));
        line(out, "base_url", toml_string(openai->base_url));
        line(out, "model", toml_string(openai->model));
        if (openai->api_key_env) {
            line(out, "api_key_env", toml_string(*openai->api_key_env));
        }
        if (openai->temperature) {
            line(out, "temperature", floating(*openai->temperature));
        }
        if (openai->reasoning_effort) {
            line(out, "reasoning_effort", toml_string(*openai->reasoning_effort));
        }
    } else if (const auto* anthropic = std::get_if<AnthropicConfig>(&model.config)) {
        line(out, "provider", toml_string("anthropic"));
        line(out, "model", toml_string(anthropic->model));
        line(out, "api_key_env", toml_string(anthropic->api_key_env));
        line(out, "max_tokens", std::to_string(anthropic->max_tokens));
        if (anthropic->base_url) {
            line(out, "base_url", toml_string(*anthropic->base_url));
        }
    } else if (const auto* claude = std::get_if<ClaudeCodeConfig>(&model.config)) {
        line(out, "provider", toml_string("claude_code"));
        if (claude->model) {
            line(out, "model", toml_string(*claude->model));
        }
        line(out, "command", toml_string(claude->command));
        line(out, "timeout_secs", std::to_string(claude->timeout_secs));
        line(out, "persistent", boolean(claude->persistent));
    }
    if (model.local) {
        line(out, "local", boolean(*model.local));
    }
}

/// Section écrite seulement si elle a au moins une ligne.
void section(std::string& out, std::string_view name, const std::string& body) {
    if (!body.empty()) {
        out += std::format("\n[{}]\n{}", name, body);
    }
}

std::string format_agent(const AgentConfig& agent) {
    const AgentConfig defaults;
    std::string body;
    if (agent.max_steps != defaults.max_steps) {
        line(body, "max_steps", std::to_string(agent.max_steps));
    }
    if (agent.system_prompt) {
        line(body, "system_prompt", toml_string(*agent.system_prompt));
    }
    if (agent.fast_path != defaults.fast_path) {
        line(body, "fast_path", boolean(agent.fast_path));
    }
    if (agent.unload_on_switch != defaults.unload_on_switch) {
        line(body, "unload_on_switch", boolean(agent.unload_on_switch));
    }
    if (agent.local_only != defaults.local_only) {
        line(body, "local_only", boolean(agent.local_only));
    }
    if (agent.verify_replies != defaults.verify_replies) {
        line(body, "verify_replies", boolean(agent.verify_replies));
    }
    return body;
}

std::string format_permissions(const PermissionPolicy& policy) {
    const PermissionPolicy defaults;
    std::string body;
    if (policy.read_only != defaults.read_only) {
        line(body, "read_only", toml_string(to_string(policy.read_only)));
    }
    if (policy.reversible != defaults.reversible) {
        line(body, "reversible", toml_string(to_string(policy.reversible)));
    }
    if (policy.sensitive != defaults.sensitive) {
        line(body, "sensitive", toml_string(to_string(policy.sensitive)));
    }
    return body;
}

std::string format_web(const WebConfig& web) {
    const WebConfig defaults;
    std::string body;
    if (web.searxng_url) {
        line(body, "searxng_url", toml_string(*web.searxng_url));
    }
    if (web.local_searxng != defaults.local_searxng) {
        line(body, "local_searxng", boolean(web.local_searxng));
    }
    if (web.searxng_idle_minutes != defaults.searxng_idle_minutes) {
        line(body, "searxng_idle_minutes", std::to_string(web.searxng_idle_minutes));
    }
    if (web.engine) {
        line(body, "engine", toml_string(*web.engine));
    }
    return body;
}

void format_module(std::string& out, std::string_view id, const ModuleSettings& settings) {
    if (!settings.enabled && settings.risk.empty()) {
        return;
    }
    out += std::format("\n[modules.{}]\n", key(id));
    if (settings.enabled) {
        line(out, "enabled", boolean(*settings.enabled));
    }
    if (!settings.risk.empty()) {
        std::string risks;
        for (const auto& [tool, level] : settings.risk) {
            risks += std::format("{}{} = {}", risks.empty() ? "" : ", ", key(tool),
                                 toml_string(to_string(level)));
        }
        line(out, "risk", "{ " + risks + " }");
    }
}

std::string format_orchestration(const OrchestrationConfig& orchestration) {
    const OrchestrationConfig defaults;
    std::string body;
    if (orchestration.enabled != defaults.enabled) {
        line(body, "enabled", boolean(orchestration.enabled));
    }
    if (orchestration.mode != defaults.mode) {
        line(body, "mode", toml_string(orchestration.mode));
    }
    if (orchestration.max_tasks != defaults.max_tasks) {
        line(body, "max_tasks", std::to_string(orchestration.max_tasks));
    }
    if (orchestration.max_agents != defaults.max_agents) {
        line(body, "max_agents", std::to_string(orchestration.max_agents));
    }
    if (orchestration.max_parallel != defaults.max_parallel) {
        line(body, "max_parallel", std::to_string(orchestration.max_parallel));
    }
    return body;
}

void format_agent_profile(std::string& out, const AgentProfile& agent) {
    out += std::format("\n[agents.{}]\n", key(agent.name));
    line(out, "role", toml_string(agent.role));
    if (!agent.instructions.empty()) {
        line(out, "instructions", toml_string(agent.instructions));
    }
    if (agent.model) {
        line(out, "model", toml_string(*agent.model));
    }
    std::string tools;
    for (const auto& tool : agent.tools) {
        tools += (tools.empty() ? "" : ", ") + toml_string(tool);
    }
    line(out, "tools", "[" + tools + "]");
    if (agent.max_steps != AgentProfile{}.max_steps) {
        line(out, "max_steps", std::to_string(agent.max_steps));
    }
}

std::unexpected<Error> write_error(const std::filesystem::path& path, std::string_view what) {
    return make_error(ErrorCode::Config,
                      std::format("écriture impossible : {} ({})", path.string(), what));
}

} // namespace

std::optional<std::filesystem::path> default_config_path() {
    std::filesystem::path base;
#ifdef _WIN32
    const auto roaming = read_env("APPDATA");
    if (!roaming) {
        return std::nullopt;
    }
    base = *roaming;
#else
    // Spécification XDG : un chemin relatif est invalide et doit être ignoré.
    if (const auto xdg = read_env("XDG_CONFIG_HOME"); xdg && xdg->starts_with('/')) {
        base = *xdg;
    } else if (const auto home = read_env("HOME"); home && home->starts_with('/')) {
        base = std::filesystem::path(*home) / ".config";
    } else {
        return std::nullopt;
    }
#endif
    return base / "avenoir" / "config.toml";
}

std::string format_config(const AppConfig& config) {
    std::string out =
        "# Configuration d'Avenoir (voir config.example.toml pour tous les réglages).\n";
    for (const auto& model : config.models) {
        format_model(out, model);
    }
    section(out, "agent", format_agent(config.agent));
    section(out, "permissions", format_permissions(config.permissions));
    section(out, "web", format_web(config.web));
    for (const auto& [id, settings] : config.modules) {
        format_module(out, id, settings);
    }
    section(out, "orchestration", format_orchestration(config.orchestration));
    for (const auto& agent : config.agents) {
        format_agent_profile(out, agent);
    }
    return out;
}

Result<void> save_config(const std::filesystem::path& path, const AppConfig& config) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path directory = path.parent_path();
    if (!directory.empty()) {
        fs::create_directories(directory, error);
        if (error) {
            return write_error(directory, error.message());
        }
        // Réglages : lisibles par l'utilisateur seul (sous Windows, %APPDATA%
        // est déjà propre à l'utilisateur).
        fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace, error);
    }
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return write_error(temporary, "création");
        }
        // Droits restreints avant d'écrire le moindre réglage.
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, error);
        file << format_config(config);
        file.flush();
        if (!file) {
            file.close();
            fs::remove(temporary, error);
            return write_error(temporary, "écriture");
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        const std::string reason = error.message();
        fs::remove(temporary, error);
        return write_error(path, reason);
    }
    return {};
}

} // namespace avenoir::core
