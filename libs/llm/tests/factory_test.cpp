#include <string>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/llm/factory.hpp"

using namespace avenoir;

TEST_CASE("fabrique : chaque configuration donne le bon fournisseur", "[factory]") {
    const auto openai =
        llm::make_provider(core::OpenAiCompatibleConfig{.base_url = "http://localhost:11434/v1",
                                                        .model = "m",
                                                        .api_key_env = std::nullopt,
                                                        .temperature = std::nullopt});
    REQUIRE(openai.has_value());
    CHECK((*openai)->name() == "openai_compatible");

    const auto claude = llm::make_provider(core::ClaudeCodeConfig{});
    REQUIRE(claude.has_value());
    CHECK((*claude)->name() == "claude_code");
}

TEST_CASE("fabrique : variable de clé absente → erreur qui cite le nom", "[factory]") {
    const auto anthropic = llm::make_provider(
        core::AnthropicConfig{.model = "m",
                              .api_key_env = "AVENOIR_TEST_VARIABLE_QUI_N_EXISTE_PAS",
                              .max_tokens = 1024,
                              .base_url = std::nullopt});
    REQUIRE_FALSE(anthropic.has_value());
    CHECK(anthropic.error().code == core::ErrorCode::Config);
    CHECK(anthropic.error().message.find("AVENOIR_TEST_VARIABLE_QUI_N_EXISTE_PAS") !=
          std::string::npos);
}
