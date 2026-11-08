#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

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
    /// Effort de réflexion des modèles qui raisonnent avant de répondre :
    /// « none », « low », « medium » ou « high ». Absent : choix du serveur.
    /// « none » réduit fortement la latence (Ollama, OpenAI…).
    std::optional<std::string> reasoning_effort;
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

/// Réglages de l'utilisateur pour un module (`[modules.<id>]`). Le module
/// lui-même se décrit dans son `module.toml` (PROJECT.md § 2.6).
struct ModuleSettings {
    /// Absent : activé pour un module fourni avec Avenoir, désactivé pour un
    /// module installé par l'utilisateur (directive de sécurité n° 15).
    std::optional<bool> enabled;
    /// Risque demandé par outil (nom court, sans le préfixe `id__`). Il ne
    /// peut que **relever** celui du manifeste : une baisse écrite ici est
    /// ignorée (elle exige un accord enregistré, PROJECT.md § 2.6).
    std::map<std::string, RiskLevel, std::less<>> risk;
};

struct AppConfig {
    LlmConfig llm;
    AgentConfig agent;
    PermissionPolicy permissions;
    /// Par identifiant de module.
    std::map<std::string, ModuleSettings, std::less<>> modules;
};

/// Lit une configuration TOML. Les erreurs indiquent la clé fautive.
[[nodiscard]] Result<AppConfig> parse_config(std::string_view toml_text);

/// Lit le fichier `path` puis appelle `parse_config`.
[[nodiscard]] Result<AppConfig> load_config(const std::filesystem::path& path);

} // namespace avenoir::core
