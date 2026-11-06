#pragma once

#include <string>
#include <string_view>

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

} // namespace detail

} // namespace avenoir::llm
