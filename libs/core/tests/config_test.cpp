#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"

using namespace avenoir::core;

TEST_CASE("config.example.toml se charge", "[config]") {
    const auto config = load_config(AVENOIR_SOURCE_DIR "/config.example.toml");
    REQUIRE(config.has_value());
    const auto* llm = std::get_if<OpenAiCompatibleConfig>(&config->llm);
    REQUIRE(llm != nullptr);
    CHECK(llm->base_url == "http://localhost:11434/v1");
    CHECK(llm->temperature == 0.2);
    CHECK(config->agent.max_steps == 10);
    CHECK(config->agent.fast_path);
    CHECK(config->permissions.sensitive == Policy::Confirm);
}

TEST_CASE("valeurs par défaut et fournisseur claude_code", "[config]") {
    const auto config = parse_config(R"(
        [llm]
        provider = "claude_code"
        model = "sonnet"
    )");
    REQUIRE(config.has_value());
    const auto* llm = std::get_if<ClaudeCodeConfig>(&config->llm);
    REQUIRE(llm != nullptr);
    CHECK(llm->model == "sonnet");
    CHECK(llm->command == "claude");
    CHECK(llm->timeout_secs == 180);
    CHECK(config->agent.max_steps == 10);
    CHECK(config->mcp_servers.empty());
}

TEST_CASE("fournisseur anthropic", "[config]") {
    const auto config = parse_config(R"(
        [llm]
        provider = "anthropic"
        model = "claude-sonnet-5-5"
        max_tokens = 2048
    )");
    REQUIRE(config.has_value());
    const auto* llm = std::get_if<AnthropicConfig>(&config->llm);
    REQUIRE(llm != nullptr);
    CHECK(llm->api_key_env == "ANTHROPIC_API_KEY");
    CHECK(llm->max_tokens == 2048);
}

TEST_CASE("un serveur MCP est sensible par défaut", "[config]") {
    const auto config = parse_config(R"(
        [llm]
        provider = "openai_compatible"
        base_url = "http://localhost:11434/v1"
        model = "m"

        [[mcp_servers]]
        name = "fs"
        command = "fs-server"
        args = ["--racine", "/tmp"]
        env = { CLE = "valeur" }
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->mcp_servers.size() == 1);
    const auto& server = config->mcp_servers.front();
    CHECK(server.risk == RiskLevel::Sensitive);
    CHECK(server.enabled);
    CHECK(server.args.size() == 2);
    CHECK(server.env.at("CLE") == "valeur");
}

TEST_CASE("les erreurs de configuration sont lisibles", "[config]") {
    const auto expect_error = [](const char* toml, const std::string& fragment) {
        const auto config = parse_config(toml);
        REQUIRE_FALSE(config.has_value());
        CHECK(config.error().code == ErrorCode::Config);
        CHECK(config.error().message.find(fragment) != std::string::npos);
    };

    expect_error("", "llm");
    expect_error("[llm]\nprovider = \"inconnu\"", "fournisseur inconnu");
    expect_error("[llm]\nprovider = \"anthropic\"", "llm.model");
    expect_error("[llm]\nprovider = \"claude_code\"\n[agent]\nmax_steps = 0", "agent.max_steps");
    expect_error("[llm]\nprovider = \"claude_code\"\n[agent]\nfast_path = \"oui\"",
                 "agent.fast_path");
    expect_error("[llm]\nprovider = \"claude_code\"\n[permissions]\nsensitive = \"parfois\"",
                 "permissions.sensitive");
    expect_error("[llm\nprovider = 1", "TOML illisible");
}
