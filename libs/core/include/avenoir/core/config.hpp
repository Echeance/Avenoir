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
    /// Effort de réflexion des modèles qui raisonnent avant de répondre :
    /// « none », « low », « medium » ou « high ». Absent : choix du serveur.
    /// « none » réduit fortement la latence (Ollama, OpenAI…).
    std::optional<std::string> reasoning_effort;

    bool operator==(const OpenAiCompatibleConfig&) const = default;
};

/// API Messages d'Anthropic.
struct AnthropicConfig {
    std::string model;
    std::string api_key_env = "ANTHROPIC_API_KEY";
    std::uint32_t max_tokens = 4096;
    std::optional<std::string> base_url;

    bool operator==(const AnthropicConfig&) const = default;
};

/// CLI Claude Code (`claude -p`) : utilise l'abonnement Claude de l'utilisateur.
struct ClaudeCodeConfig {
    std::optional<std::string> model;
    std::string command = "claude";
    std::uint32_t timeout_secs = 180;
    /// Garder un processus `claude` ouvert d'une requête à l'autre (beaucoup
    /// plus rapide) ; faux : un lancement par appel.
    bool persistent = true;

    bool operator==(const ClaudeCodeConfig&) const = default;
};

/// Fournisseur d'un modèle (champ `provider` de `[models.<nom>]`).
using LlmConfig = std::variant<OpenAiCompatibleConfig, AnthropicConfig, ClaudeCodeConfig>;

/// Modèle nommé (`[models.<nom>]`), parmi ceux entre lesquels l'utilisateur
/// peut basculer.
struct NamedLlm {
    std::string name; ///< Minuscules ASCII, chiffres, « - » et « _ ».
    LlmConfig config;
    /// `local = true` : serveur `openai_compatible` du réseau local tenu pour
    /// local. Absent : local seulement si l'adresse est celle de la machine.
    std::optional<bool> local;

    bool operator==(const NamedLlm&) const = default;
};

struct AgentConfig {
    std::size_t max_steps = 10;
    std::optional<std::string> system_prompt; ///< Remplace le prompt par défaut.
    bool fast_path = true;
    /// En changeant de modèle, décharger l'ancien s'il est local et servi
    /// par le même serveur que le nouveau (libère sa mémoire vidéo).
    bool unload_on_switch = true;
    /// Mode « local uniquement » (directive de sécurité n° 10) : aucun modèle
    /// distant n'est utilisé, ni au lancement ni par un changement de modèle.
    bool local_only = false;
    /// Vérifier les réponses du modèle : une action annoncée sans l'appel
    /// d'outil correspondant relance le modèle, puis est signalée.
    bool verify_replies = true;
    /// Commandes shell (outil `run_command`) : désactivées par défaut ; chaque
    /// commande est analysée et toujours confirmée (décision 60).
    bool shell = false;

    bool operator==(const AgentConfig&) const = default;
};

/// Réglages de l'utilisateur pour un module (`[modules.<id>]`). Le module
/// lui-même se décrit dans son `module.toml` (docs/PROJECT.md § 2.6).
struct ModuleSettings {
    /// Absent : activé pour un module fourni avec Avenoir, désactivé pour un
    /// module installé par l'utilisateur (directive de sécurité n° 15).
    std::optional<bool> enabled;
    /// Risque demandé par outil (nom court, sans le préfixe `id__`). Il ne
    /// peut que **relever** celui du manifeste : une baisse écrite ici est
    /// ignorée (elle exige un accord enregistré, docs/PROJECT.md § 2.6).
    std::map<std::string, RiskLevel, std::less<>> risk;

    bool operator==(const ModuleSettings&) const = default;
};

/// Recherche web (`[web]`).
struct WebConfig {
    /// Instance SearXNG (`http(s)://…`, locale permise : choisie par
    /// l'utilisateur) ; absente : instance locale lancée par Avenoir.
    std::optional<std::string> searxng_url;
    /// Lancer une instance SearXNG locale (podman ou docker) à la première
    /// recherche, si aucune n'est configurée.
    bool local_searxng = true;
    /// Arrêt de l'instance locale après ce nombre de minutes sans recherche.
    int searxng_idle_minutes = 10;
    /// Moteur de départ : « searxng » ou « duckduckgo ». Absent : SearXNG
    /// s'il est disponible, sinon DuckDuckGo. Un moteur choisi depuis Avenoir
    /// est retenu et l'emporte.
    std::optional<std::string> engine;

    bool operator==(const WebConfig&) const = default;
};

/// Agent de l'équipe de l'utilisateur (`[agents.<nom>]`, docs/PROJECT.md § 2.11).
struct AgentProfile {
    std::string name;                 ///< Minuscules, chiffres, « - » et « _ » (40 au plus).
    std::string role;                 ///< « développeur », « relecteur »…
    std::string instructions;         ///< Consigne propre à l'agent.
    std::optional<std::string> model; ///< Un `[models.<nom>]` ; absent : le modèle actif.
    std::vector<std::string> tools;   ///< Outils permis (noms exacts).
    std::size_t max_steps = 8;

    bool operator==(const AgentProfile&) const = default;
};

/// Orchestrateur multi-agents (`[orchestration]`).
struct OrchestrationConfig {
    bool enabled = true; ///< Faux : pas d'outil de délégation.
    /// « auto » (agents créés à la volée), « configured » (équipe de
    /// l'utilisateur seulement) ou « both ».
    std::string mode = "both";
    std::size_t max_tasks = 12;
    std::size_t max_agents = 6;   ///< Agents créés à la volée par plan.
    std::size_t max_parallel = 3; ///< Tâches en même temps.

    bool operator==(const OrchestrationConfig&) const = default;
};

struct AppConfig {
    /// Modèles (`[models.<nom>]`), dans l'ordre du fichier : le premier sert
    /// par défaut. Aucun : pas de LLM, seule la voie rapide répond.
    std::vector<NamedLlm> models;
    AgentConfig agent;
    PermissionPolicy permissions;
    WebConfig web;
    /// Par identifiant de module.
    std::map<std::string, ModuleSettings, std::less<>> modules;
    /// Équipe de l'utilisateur, dans l'ordre du fichier.
    std::vector<AgentProfile> agents;
    OrchestrationConfig orchestration;

    bool operator==(const AppConfig&) const = default;
};

/// Lit une configuration TOML. Les erreurs indiquent la clé fautive.
[[nodiscard]] Result<AppConfig> parse_config(std::string_view toml_text);

/// Lit le fichier `path` puis appelle `parse_config`.
[[nodiscard]] Result<AppConfig> load_config(const std::filesystem::path& path);

/// Emplacement standard de la configuration : `$XDG_CONFIG_HOME/avenoir/config.toml`
/// (`~/.config/avenoir/config.toml` par défaut), `%APPDATA%\avenoir\config.toml`
/// sous Windows. Rien si l'environnement ne permet pas de le déterminer.
/// Le dossier courant n'est jamais lu : un `config.toml` déposé dans un
/// dossier quelconque ne doit pas pouvoir détourner Avenoir.
[[nodiscard]] std::optional<std::filesystem::path> default_config_path();

/// Texte TOML de `config`, relu à l'identique par `parse_config`. Les modèles
/// sont écrits en entier, dans leur ordre ; pour le reste, seules les valeurs
/// qui diffèrent des réglages par défaut (qui peuvent ainsi évoluer).
[[nodiscard]] std::string format_config(const AppConfig& config);

/// Écrit `config` dans `path` : dossier créé (lisible par l'utilisateur seul),
/// fichier `0600`, remplacement atomique (fichier temporaire puis renommage).
[[nodiscard]] Result<void> save_config(const std::filesystem::path& path, const AppConfig& config);

} // namespace avenoir::core
