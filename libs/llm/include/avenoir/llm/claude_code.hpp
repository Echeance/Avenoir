#pragma once

#include <span>
#include <string>
#include <string_view>

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
/// structurée (`--json-schema`). Chaque appel est sans état : tout l'historique
/// est renvoyé.
class ClaudeCode final : public core::LlmProvider {
  public:
    explicit ClaudeCode(core::ClaudeCodeConfig config);

    [[nodiscard]] std::string_view name() const override { return "claude_code"; }
    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& request) override;

  private:
    core::ClaudeCodeConfig config_;
};

namespace detail {

/// Schéma imposé à la réponse : `{ text, tool_calls: [{ name, arguments }] }`.
[[nodiscard]] nlohmann::json claude_code_response_schema();
/// Consignes de format ajoutées au prompt système.
[[nodiscard]] std::string claude_code_system_prompt(std::string_view system);
/// Outils et conversation mis en texte (envoyé sur l'entrée standard).
[[nodiscard]] std::string claude_code_prompt(std::span<const core::ToolSpec> tools,
                                             std::span<const core::Message> messages);
/// Lecture de la sortie `--output-format json` de la CLI.
[[nodiscard]] core::Result<core::LlmResponse>
parse_claude_code_output(const nlohmann::json& output);

} // namespace detail

} // namespace avenoir::llm
