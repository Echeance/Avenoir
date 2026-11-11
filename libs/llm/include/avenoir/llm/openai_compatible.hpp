#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/llm.hpp"

namespace avenoir::llm {

/// Toute API compatible OpenAI `/chat/completions` : Ollama, llama.cpp server,
/// LM Studio, vLLM, OpenAI, Mistral, OpenRouter…
class OpenAiCompatible final : public core::LlmProvider {
  public:
    OpenAiCompatible(core::OpenAiCompatibleConfig config, std::optional<std::string> api_key);

    [[nodiscard]] std::string_view name() const override { return "openai_compatible"; }
    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& request) override;

  private:
    core::OpenAiCompatibleConfig config_;
    std::optional<std::string> api_key_;
};

namespace detail {

/// Corps JSON de la requête (exposé pour les tests).
[[nodiscard]] nlohmann::json openai_request_body(const core::OpenAiCompatibleConfig& config,
                                                 const core::LlmRequest& request);
/// Lecture de la réponse (exposée pour les tests).
[[nodiscard]] core::Result<core::LlmResponse> parse_openai_response(const nlohmann::json& body);

/// Réponse reçue en flux (`"stream": true`) : chaque ligne `data:` est donnée
/// à `feed`, qui passe le texte à `sink` au fil de l'eau et assemble les
/// appels d'outils (arguments reçus par morceaux) ; `finish` rend la réponse
/// complète.
class OpenAiStream {
  public:
    [[nodiscard]] core::Result<void> feed(std::string_view data, const core::TextSink& sink);
    [[nodiscard]] core::Result<core::LlmResponse> finish();

  private:
    struct PendingCall {
        std::string id;
        std::string name;
        std::string arguments;
    };
    core::LlmResponse response_;
    std::vector<PendingCall> calls_;
    bool received_ = false;
};

} // namespace detail

} // namespace avenoir::llm
