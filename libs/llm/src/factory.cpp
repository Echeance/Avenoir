#include "avenoir/llm/factory.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "avenoir/core/env.hpp"
#include "avenoir/llm/anthropic.hpp"
#include "avenoir/llm/claude_code.hpp"
#include "avenoir/llm/openai_compatible.hpp"

namespace avenoir::llm {

namespace {

using ProviderResult = core::Result<std::unique_ptr<core::LlmProvider>>;

/// Clé lue dans la variable `name` ; le message d'erreur cite le nom de la
/// variable, jamais une valeur.
core::Result<std::string> read_key(const std::string& name) {
    if (auto value = core::read_env(name)) {
        return std::move(*value);
    }
    return core::make_error(core::ErrorCode::Config,
                            "variable d'environnement « " + name + " » absente ou vide");
}

} // namespace

ProviderResult make_provider(const core::LlmConfig& config) {
    if (const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&config)) {
        std::optional<std::string> key;
        if (openai->api_key_env) {
            auto value = read_key(*openai->api_key_env);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            key = std::move(*value);
        }
        return std::make_unique<OpenAiCompatible>(*openai, std::move(key));
    }
    if (const auto* anthropic = std::get_if<core::AnthropicConfig>(&config)) {
        auto key = read_key(anthropic->api_key_env);
        if (!key) {
            return std::unexpected(std::move(key.error()));
        }
        return std::make_unique<Anthropic>(*anthropic, std::move(*key));
    }
    if (const auto* claude = std::get_if<core::ClaudeCodeConfig>(&config)) {
        return std::make_unique<ClaudeCode>(*claude);
    }
    return core::make_error(core::ErrorCode::Config, "fournisseur LLM inconnu");
}

} // namespace avenoir::llm
