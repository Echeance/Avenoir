#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/llm.hpp"

namespace avenoir::llm {

/// Fournisseur « Claude Code » : passe par la CLI officielle `claude -p`,
/// connectée à l'abonnement Claude (Pro / Max) de l'utilisateur, sans clé d'API.
///
/// Claude Code sert uniquement de moteur de langage : ses outils, serveurs MCP,
/// hooks et réglages sont désactivés, il ne peut rien faire sur la machine. Les
/// outils d'Avenoir lui sont décrits dans le prompt et il répond par une sortie
/// structurée (`--json-schema`).
///
/// Par défaut (`persistent`), un processus `claude` reste ouvert
/// (`--input-format stream-json`) : il garde la conversation, et Avenoir ne
/// lui envoie que les nouveaux messages, ce qui évite le démarrage de la CLI
/// à chaque appel. Si la conversation ne prolonge plus celle de la session
/// (autre conversation, réponse écartée, prompt système changé), la session
/// est relancée avec tout l'historique. Deux sessions au plus (par prompt
/// système et jeu d'outils), arrêtées avec le fournisseur : en changeant de
/// modèle, l'agent le détruit, et le processus `claude` s'arrête.
class ClaudeCode final : public core::LlmProvider {
  public:
    explicit ClaudeCode(core::ClaudeCodeConfig config);
    ~ClaudeCode() override;
    ClaudeCode(const ClaudeCode&) = delete;
    ClaudeCode& operator=(const ClaudeCode&) = delete;
    ClaudeCode(ClaudeCode&&) = delete;
    ClaudeCode& operator=(ClaudeCode&&) = delete;

    [[nodiscard]] std::string_view name() const override { return "claude_code"; }
    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& request) override;

  private:
    struct Session;

    [[nodiscard]] core::Result<core::LlmResponse> complete_once(const core::LlmRequest& request);
    [[nodiscard]] core::Result<core::LlmResponse>
    complete_persistent(const core::LlmRequest& request);
    [[nodiscard]] std::vector<std::string> base_arguments(std::string_view system) const;

    core::ClaudeCodeConfig config_;
    std::vector<std::unique_ptr<Session>> sessions_; ///< La plus récente à la fin.
    std::uint64_t turn_ = 0;                         ///< Pour des identifiants d'appels uniques.
};

namespace detail {

/// Schéma imposé à la réponse : `{ text, tool_calls: [{ name, arguments }] }`.
[[nodiscard]] nlohmann::json claude_code_response_schema();
/// Consignes de format ajoutées au prompt système.
[[nodiscard]] std::string claude_code_system_prompt(std::string_view system);
/// Outils et conversation mis en texte (envoyé sur l'entrée standard).
[[nodiscard]] std::string claude_code_prompt(std::span<const core::ToolSpec> tools,
                                             std::span<const core::Message> messages);
/// Lecture de la sortie `--output-format json` de la CLI (ou de l'évènement
/// `result` en flux).
[[nodiscard]] core::Result<core::LlmResponse>
parse_claude_code_output(const nlohmann::json& output);

/// Messages à envoyer à une session qui connaît déjà `known` : la suite de
/// `known` dans `messages`. Rien si `messages` ne prolonge pas `known`, ou
/// n'y ajoute rien : la session doit être relancée.
[[nodiscard]] std::optional<std::span<const core::Message>>
claude_code_delta(std::span<const core::Message> known, std::span<const core::Message> messages);

/// Nouveaux messages (résultats d'outils, question de l'utilisateur) mis en
/// texte pour une session déjà ouverte, sans redécrire les outils.
[[nodiscard]] std::string claude_code_continuation(std::span<const core::Message> messages);

/// Message d'erreur de Claude Code, avec la marche à suivre si la CLI n'est
/// pas connectée à un compte (« lancez `claude` puis `/login` »).
[[nodiscard]] std::string claude_code_error(std::string_view message);

} // namespace detail

} // namespace avenoir::llm
