#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"

#include "setup_plan.hpp"

using namespace avenoir;
using daemon::SetupFindings;

namespace {

const core::OpenAiCompatibleConfig& openai(const daemon::SetupCandidate& candidate) {
    return std::get<core::OpenAiCompatibleConfig>(candidate.model.config);
}

} // namespace

TEST_CASE("model_name_for : nom de section valide et unique", "[setup]") {
    CHECK(daemon::model_name_for("gemma4:26b-a4b", {}) == "gemma4");
    CHECK(daemon::model_name_for("qwen3.8:27b-q4_K_M", {}) == "qwen3-8");
    CHECK(daemon::model_name_for("lmstudio-community/Mistral-7B.Q4", {}) == "mistral-7b-q4");
    CHECK(daemon::model_name_for("gemma4:e4b", {"gemma4"}) == "gemma4-2");
    CHECK(daemon::model_name_for("gemma4:2b", {"gemma4", "gemma4-2"}) == "gemma4-3");
    CHECK(daemon::model_name_for("Модель", {}) == "modele");
    CHECK(daemon::model_name_for(std::string(80, 'a'), {}).size() <= 32);
    // Toujours accepté par l'analyse de la configuration.
    const auto parsed = core::parse_config("[models." + daemon::model_name_for("Ünï code:1", {}) +
                                           "]\nprovider = \"claude_code\"\n");
    CHECK(parsed.has_value());
}

TEST_CASE("make_candidates : conseillés d'abord, distants à la fin", "[setup]") {
    SetupFindings findings;
    findings.local_servers.push_back(
        {.base_url = "http://127.0.0.1:11434/v1",
         .models = {"qwen2.5vl:7b", "ministral-3:8b", "gemma4:26b-a4b"}});
    findings.owned_servers.push_back(
        {.base_url = "http://192.168.1.20:1234/v1", .models = {"gemma4:26b-a4b"}});
    findings.claude_code = true;
    findings.anthropic_key = true;

    const auto candidates = daemon::make_candidates(findings, /*local_only=*/false);
    REQUIRE(candidates.size() == 6);
    CHECK(openai(candidates[0]).model == "gemma4:26b-a4b");
    CHECK(candidates[0].recommended);
    CHECK(candidates[0].model.name == "gemma4");
    CHECK(openai(candidates[0]).reasoning_effort == "none");         // Ollama
    CHECK_FALSE(candidates[0].model.local.has_value());              // cette machine : déjà locale
    CHECK(candidates[1].model.name == "gemma4-2");                   // même modèle, autre serveur
    CHECK(candidates[1].model.local == true);                        // serveur du réseau, à soi
    CHECK_FALSE(openai(candidates[1]).reasoning_effort.has_value()); // pas Ollama
    CHECK(candidates[1].label.contains("192.168.1.20"));
    CHECK(openai(candidates[2]).model == "ministral-3:8b");
    CHECK(openai(candidates[3]).model == "qwen2.5vl:7b");
    CHECK_FALSE(candidates[3].recommended);
    CHECK(std::holds_alternative<core::ClaudeCodeConfig>(candidates[4].model.config));
    CHECK(candidates[4].remote);
    CHECK(std::holds_alternative<core::AnthropicConfig>(candidates[5].model.config));
    CHECK(candidates[5].remote);

    const auto local = daemon::make_candidates(findings, /*local_only=*/true);
    CHECK(local.size() == 4);
    for (const auto& candidate : local) {
        CHECK_FALSE(candidate.remote);
    }
}

TEST_CASE("make_candidates : serveur saisi mais pas à soi, tenu pour distant", "[setup]") {
    SetupFindings findings;
    findings.remote_servers.push_back(
        {.base_url = "https://llm.exemple.org/v1", .models = {"mistral-large"}});
    const auto candidates = daemon::make_candidates(findings, false);
    REQUIRE(candidates.size() == 1);
    CHECK(candidates[0].remote);
    CHECK_FALSE(candidates[0].model.local.has_value());
    CHECK(daemon::make_candidates(findings, true).empty());
}

TEST_CASE("parse_selection", "[setup]") {
    using List = std::vector<std::size_t>;
    CHECK(daemon::parse_selection("1", 3) == List{0});
    CHECK(daemon::parse_selection("3 1", 3) == List{2, 0});
    CHECK(daemon::parse_selection("2,3, 2", 3) == List{1, 2});
    CHECK_FALSE(daemon::parse_selection("", 3).has_value());
    CHECK_FALSE(daemon::parse_selection("0", 3).has_value());
    CHECK_FALSE(daemon::parse_selection("4", 3).has_value());
    CHECK_FALSE(daemon::parse_selection("1 a", 3).has_value());
    CHECK_FALSE(daemon::parse_selection("-1", 3).has_value());
    CHECK_FALSE(daemon::parse_selection("99999999999999999999999", 3).has_value());
}

TEST_CASE("build_setup_config : relu à l'identique", "[setup]") {
    SetupFindings findings;
    findings.local_servers.push_back(
        {.base_url = "http://127.0.0.1:11434/v1", .models = {"gemma4:26b-a4b", "ministral-3:8b"}});
    const auto candidates = daemon::make_candidates(findings, false);
    const auto config = daemon::build_setup_config({candidates[1], candidates[0]}, true);
    REQUIRE(config.models.size() == 2);
    CHECK(config.models[0].name == "ministral-3");
    CHECK(config.agent.local_only);
    const auto parsed = core::parse_config(core::format_config(config));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == config);
}

TEST_CASE("normalize_server_url", "[setup]") {
    CHECK(daemon::normalize_server_url("http://192.168.1.20:11434") ==
          "http://192.168.1.20:11434/v1");
    CHECK(daemon::normalize_server_url(" http://pc:1234/v1/ ") == "http://pc:1234/v1");
    CHECK(daemon::normalize_server_url("https://llm.exemple.org") == "https://llm.exemple.org/v1");
    CHECK_FALSE(daemon::normalize_server_url("ftp://pc:21").has_value());
    CHECK_FALSE(daemon::normalize_server_url("pc:11434").has_value());
    CHECK_FALSE(daemon::normalize_server_url("http://moi:secret@pc:11434").has_value());
    CHECK_FALSE(daemon::normalize_server_url("http://pc:11434/v1\x1b[2J").has_value());
    CHECK_FALSE(daemon::normalize_server_url("http://p c:1").has_value());
}

TEST_CASE("build_setup_config : les autres réglages de la base sont gardés", "[setup]") {
    core::AppConfig base;
    base.models.push_back(core::NamedLlm{
        .name = "ancien", .config = core::ClaudeCodeConfig{}, .local = std::nullopt});
    base.permissions.reversible = core::Policy::Confirm;
    base.web.engine = "duckduckgo";
    base.modules["meteo"].enabled = false;
    base.agent.max_steps = 3;

    const auto none = daemon::build_setup_config({}, /*local_only=*/true, base);
    CHECK(none.models.empty()); // « aucun » : voie rapide seule
    CHECK(none.agent.local_only);
    CHECK(none.agent.max_steps == 3);
    CHECK(none.permissions.reversible == core::Policy::Confirm);
    CHECK(none.web.engine == "duckduckgo");
    CHECK(none.modules.at("meteo").enabled == false);
}
