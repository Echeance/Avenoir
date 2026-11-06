#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::core {

/// Toute API compatible OpenAI `/chat/completions` : Ollama, llama.cpp server,
/// LM Studio, vLLM, OpenAI, Mistral, OpenRouter…
struct OpenAiCompatibleConfig {
    std::string base_url;
    std::string model;
    std::optional<std::string> api_key_env; ///< *Nom* de la variable, jamais la clé.
    std::optional<double> temperature;
};

/// API Messages d'Anthropic.
struct AnthropicConfig {
    std::string model;
    std::string api_key_env = "ANTHROPIC_API_KEY";
    std::uint32_t max_tokens = 4096;
    std::optional<std::string> base_url;
};

/// CLI Claude Code (`claude -p`) : utilise l'abonnement Claude de l'utilisateur.
struct ClaudeCodeConfig {
    std::optional<std::string> model;
    std::string command = "claude";
    std::uint32_t timeout_secs = 180;
};

/// Fournisseur choisi par l'utilisateur (champ `provider` de `[llm]`).
using LlmConfig = std::variant<OpenAiCompatibleConfig, AnthropicConfig, ClaudeCodeConfig>;

struct AgentConfig {
    std::size_t max_steps = 10;
    std::optional<std::string> system_prompt; ///< Remplace le prompt par défaut.
    bool fast_path = true;
};

/// Serveur MCP lancé en sous-processus (transport stdio).
struct McpServerConfig {
    std::string name; ///< Préfixe des outils : `nom__outil`.
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    /// Appliqué à tous les outils du serveur. Par défaut `Sensitive` : on ne
    /// fait pas confiance à ce qu'un serveur déclare sur lui-même.
    RiskLevel risk = RiskLevel::Sensitive;
    bool enabled = true;
};

struct AppConfig {
    LlmConfig llm;
    AgentConfig agent;
    PermissionPolicy permissions;
    std::vector<McpServerConfig> mcp_servers;
};

/// Lit une configuration TOML. Les erreurs indiquent la clé fautive.
[[nodiscard]] Result<AppConfig> parse_config(std::string_view toml_text);

/// Lit le fichier `path` puis appelle `parse_config`.
[[nodiscard]] Result<AppConfig> load_config(const std::filesystem::path& path);

} // namespace avenoir::core
