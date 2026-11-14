#include <string>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/llm/store.hpp"

using namespace avenoir;

TEST_CASE("magasin : catalogue conseillé par le banc", "[store]") {
    const auto catalog = llm::model_catalog();
    REQUIRE(catalog.size() >= 3);
    CHECK(catalog.front().tag == "gemma4:26b-a4b");
    for (const auto& model : catalog) {
        CHECK(llm::is_valid_model_tag(model.tag));
        CHECK(llm::config_name_for(model.tag) == model.name);
        CHECK(model.disk_gb > 0);
    }
    CHECK(llm::find_catalog_model("ministral-3") != nullptr);
    CHECK(llm::find_catalog_model("ministral-3:8b") != nullptr);
    CHECK(llm::find_catalog_model("inconnu") == nullptr);
}

TEST_CASE("magasin : étiquettes refusées et noms de configuration", "[store]") {
    CHECK(llm::is_valid_model_tag("qwen3:4b"));
    CHECK(llm::is_valid_model_tag("richardyoung/agents-a1-4b:latest"));
    CHECK_FALSE(llm::is_valid_model_tag(""));
    CHECK_FALSE(llm::is_valid_model_tag("../etc"));
    CHECK_FALSE(llm::is_valid_model_tag("a b"));
    CHECK_FALSE(llm::is_valid_model_tag("x;rm -rf"));
    CHECK_FALSE(llm::is_valid_model_tag("-option"));
    CHECK_FALSE(llm::is_valid_model_tag(std::string(101, 'a')));
    CHECK(llm::config_name_for("qwen3:4b") == "qwen3-4b");
    CHECK(llm::config_name_for("Org/Model.V2:Q4") == "org-model-v2-q4");
}

TEST_CASE("magasin : serveur Ollama de la configuration", "[store]") {
    core::AppConfig config;
    CHECK(llm::ollama_server(config) == "http://localhost:11434");
    config.models.push_back(core::NamedLlm{
        .name = "gemma",
        .config = core::OpenAiCompatibleConfig{.base_url = "http://192.168.1.20:11434/v1/",
                                               .model = "gemma4:26b-a4b",
                                               .api_key_env = std::nullopt,
                                               .temperature = std::nullopt,
                                               .reasoning_effort = std::nullopt},
        .local = true});
    CHECK(llm::ollama_server(config) == "http://192.168.1.20:11434");
}
