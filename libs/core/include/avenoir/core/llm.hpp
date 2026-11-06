#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/core/message.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::core {

/// Requête envoyée au LLM. Les vues restent valides pendant tout l'appel.
struct LlmRequest {
    std::string_view system;
    std::span<const Message> messages;
    std::span<const ToolSpec> tools;
    std::stop_token stop; ///< Annulation demandée par l'utilisateur.
};

/// Consommation d'un appel au LLM, quand le fournisseur la communique.
struct Usage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::optional<double> cost_usd = std::nullopt; ///< Coût indicatif (prix publics).

    bool operator==(const Usage&) const = default;
};

struct LlmResponse {
    std::string text;                 ///< Peut être vide si le modèle appelle des outils.
    std::vector<ToolCall> tool_calls; ///< Vide si c'est la réponse finale.
    std::optional<Usage> usage = std::nullopt;
};

/// Fournisseur LLM (API compatible OpenAI, Anthropic, Claude Code…). Les
/// implémentations concrètes vivent dans `avenoir::llm`.
class LlmProvider {
  public:
    LlmProvider() = default;
    LlmProvider(const LlmProvider&) = delete;
    LlmProvider& operator=(const LlmProvider&) = delete;
    LlmProvider(LlmProvider&&) = delete;
    LlmProvider& operator=(LlmProvider&&) = delete;
    virtual ~LlmProvider() = default;

    /// Nom lisible du fournisseur (pour les logs).
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual Result<LlmResponse> complete(const LlmRequest& request) = 0;
};

} // namespace avenoir::core
