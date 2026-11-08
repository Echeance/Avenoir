#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"

using namespace avenoir::core;

TEST_CASE("config.example.toml se charge", "[config]") {
    const auto config = load_config(AVENOIR_SOURCE_DIR "/config.example.toml");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 2);
    CHECK(config->models[0].name == "gemma"); // le premier du fichier sert par défaut
    CHECK(config->models[1].name == "leger");
    const auto* llm = std::get_if<OpenAiCompatibleConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->base_url == "http://localhost:11434/v1");
    CHECK(llm->temperature == 0.2);
    CHECK(config->agent.max_steps == 10);
    CHECK(config->agent.fast_path);
    CHECK(config->permissions.sensitive == Policy::Confirm);
}

TEST_CASE("valeurs par défaut et fournisseur claude_code", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "claude_code"
        model = "sonnet"
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 1);
    const auto* llm = std::get_if<ClaudeCodeConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->model == "sonnet");
    CHECK(llm->command == "claude");
    CHECK(llm->timeout_secs == 180);
    CHECK(config->agent.max_steps == 10);
    CHECK(config->modules.empty());
}

TEST_CASE("effort de réflexion d'un modèle compatible OpenAI", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "openai_compatible"
        base_url = "http://localhost:11434/v1"
        model = "qwen3.5:9b"
        reasoning_effort = "none"
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 1);
    const auto* llm = std::get_if<OpenAiCompatibleConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->reasoning_effort == "none");

    const auto invalid = parse_config(R"(
        [models.m]
        provider = "openai_compatible"
        base_url = "http://localhost:11434/v1"
        model = "m"
        reasoning_effort = "maximum"
    )");
    REQUIRE_FALSE(invalid.has_value());
    CHECK(describe(invalid.error()).contains("models.m.reasoning_effort"));
}

TEST_CASE("fournisseur anthropic", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "anthropic"
        model = "claude-sonnet-5-5"
        max_tokens = 2048
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 1);
    const auto* llm = std::get_if<AnthropicConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->api_key_env == "ANTHROPIC_API_KEY");
    CHECK(llm->max_tokens == 2048);
}

TEST_CASE("réglages des modules", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "claude_code"

        [modules.meteo]
        enabled = false

        [modules.calendrier]
        risk = { update_event = "sensitive", list_events = "reversible" }
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->modules.size() == 2);
    const auto& meteo = config->modules.at("meteo");
    CHECK(meteo.enabled == false);
    CHECK(meteo.risk.empty());
    const auto& calendar = config->modules.at("calendrier");
    CHECK_FALSE(calendar.enabled.has_value());
    CHECK(calendar.risk.at("update_event") == RiskLevel::Sensitive);
    CHECK(calendar.risk.at("list_events") == RiskLevel::Reversible);
}

TEST_CASE("les erreurs de configuration sont lisibles", "[config]") {
    const auto expect_error = [](const char* toml, const std::string& fragment) {
        const auto config = parse_config(toml);
        INFO(toml);
        REQUIRE_FALSE(config.has_value());
        CHECK(config.error().code == ErrorCode::Config);
        CHECK(config.error().message.find(fragment) != std::string::npos);
    };

    expect_error("[llm]\nprovider = \"claude_code\"", "remplacée par [models.<nom>]");
    expect_error("[models.m]\nprovider = \"inconnu\"", "fournisseur inconnu");
    expect_error("[models.m]\nprovider = \"anthropic\"", "models.m.model");
    expect_error("[models.m]\nprovider = \"claude_code\"\n[agent]\nmax_steps = 0",
                 "agent.max_steps");
    expect_error("[models.m]\nprovider = \"claude_code\"\n[agent]\nfast_path = \"oui\"",
                 "agent.fast_path");
    expect_error("[models.m]\nprovider = \"claude_code\"\n[permissions]\nsensitive = \"parfois\"",
                 "permissions.sensitive");
    expect_error("[llm\nprovider = 1", "TOML illisible");
    constexpr const char* kLlm = "[models.m]\nprovider = \"claude_code\"\n";
    expect_error((std::string("modules = 3\n") + kLlm).c_str(), "modules");
    expect_error((std::string(kLlm) + "[modules.meteo]\nactif = true").c_str(),
                 "modules.meteo.actif");
    expect_error((std::string(kLlm) + "[modules.meteo]\nrisk = { x = \"aucun\" }").c_str(),
                 "modules.meteo.risk.x");
    expect_error(
        (std::string(kLlm) + "[modules.meteo]\nrisk = { \"a b\" = \"sensitive\" }").c_str(),
        "nom d'outil invalide");
}

TEST_CASE("modèles dans l'ordre du fichier, ou aucun", "[config]") {
    const auto config = parse_config(R"(
        [models.zeta]
        provider = "openai_compatible"
        base_url = "http://192.168.1.15:11434/v1"
        model = "ministral-3:8b"
        local = true

        [models.alpha]
        provider = "claude_code"
        model = "haiku"
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 2);
    // Ordre du fichier, pas l'ordre alphabétique : « zeta » sert par défaut.
    CHECK(config->models[0].name == "zeta");
    CHECK(config->models[0].local == true);
    CHECK(config->models[1].name == "alpha");
    CHECK(std::holds_alternative<ClaudeCodeConfig>(config->models[1].config));

    // Aucun modèle : configuration valide, seule la voie rapide répondra.
    const auto empty = parse_config("[agent]\nmax_steps = 5\n");
    REQUIRE(empty.has_value());
    CHECK(empty->models.empty());
    CHECK(parse_config("").has_value());
}

TEST_CASE("modèles : erreurs lisibles", "[config]") {
    const auto error_of = [](const std::string& text) {
        const auto config = parse_config(text);
        REQUIRE_FALSE(config.has_value());
        return config.error().message;
    };
    CHECK(error_of("[models.Gros]\nprovider = \"claude_code\"\n").contains("models.Gros"));
    CHECK(error_of("[models.\"../x\"]\nprovider = \"claude_code\"\n").contains("nom invalide"));
    CHECK(error_of("[models.x]\nprovider = \"openai_compatible\"\nmodel = \"m\"\n")
              .contains("models.x.base_url"));
    CHECK(error_of("[models.x]\nprovider = \"claude_code\"\nlocal = true\n")
              .contains("models.x.local"));
    CHECK(error_of("models = 3\n").contains("models"));
}
