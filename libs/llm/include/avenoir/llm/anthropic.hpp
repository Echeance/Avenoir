#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/llm.hpp"

namespace avenoir::llm {

/// API Messages d'Anthropic.
class Anthropic final : public core::LlmProvider {
  public:
    Anthropic(core::AnthropicConfig config, std::string api_key);

    [[nodiscard]] std::string_view name() const override { return "anthropic"; }
    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& request) override;

  private:
    core::AnthropicConfig config_;
    std::string api_key_;
};

namespace detail {

/// Corps JSON de la requête (exposé pour les tests). Les messages consécutifs
/// de même rôle sont fusionnés : l'API exige l'alternance user / assistant.
[[nodiscard]] nlohmann::json anthropic_request_body(const core::AnthropicConfig& config,
                                                    const core::LlmRequest& request);
/// Lecture de la réponse (exposée pour les tests).
[[nodiscard]] core::Result<core::LlmResponse> parse_anthropic_response(const nlohmann::json& body);

/// Réponse reçue en flux (`"stream": true`, évènements `message_start`,
/// `content_block_start`, `content_block_delta`, `message_delta`…) : le
/// texte passe à `sink` au fil de l'eau, les entrées des outils (JSON reçu
/// par morceaux) sont assemblées ; `finish` rend la réponse complète.
class AnthropicStream {
  public:
    [[nodiscard]] core::Result<void> feed(std::string_view data, const core::TextSink& sink);
    [[nodiscard]] core::Result<core::LlmResponse> finish();

  private:
    struct Block {
        bool tool = false;
        std::string id;
        std::string name;
        std::string input; ///< JSON partiel des arguments.
    };
    core::LlmResponse response_;
    std::vector<Block> blocks_;
    bool received_ = false;
};

} // namespace detail

} // namespace avenoir::llm
