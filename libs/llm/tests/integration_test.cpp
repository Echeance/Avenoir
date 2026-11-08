// Tests d'intégration avec un vrai LLM. Masqués par défaut (étiquette
// « [.integration] ») : ctest ne les lance pas. Pour les lancer :
//
//   AVENOIR_TEST_CONFIG=chemin/config.toml build/debug/libs/llm/avenoir_llm_tests "[.integration]"
//
// Le fournisseur testé est celui du fichier de configuration.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/env.hpp"
#include "avenoir/llm/factory.hpp"

using namespace avenoir;

TEST_CASE("intégration : un vrai LLM répond", "[.integration]") {
    const auto path = core::read_env("AVENOIR_TEST_CONFIG");
    if (!path) {
        SKIP("AVENOIR_TEST_CONFIG non défini");
    }
    const auto config = core::load_config(*path);
    REQUIRE(config.has_value());
    REQUIRE_FALSE(config->models.empty());
    auto provider = llm::make_provider(config->models.front().config); // le premier modèle
    REQUIRE(provider.has_value());

    const std::vector<core::Message> messages{
        core::UserMessage{.content = "Réponds uniquement par le mot : pingouin"}};
    const auto response = (*provider)->complete(core::LlmRequest{
        .system = "Tu es un test.", .messages = messages, .tools = {}, .stop = {}});
    if (!response) {
        FAIL(core::describe(response.error()));
    }
    CHECK_FALSE(response->text.empty());
    UNSCOPED_INFO("réponse : " << response->text);
}
