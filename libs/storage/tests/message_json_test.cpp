#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/storage/store.hpp"

using namespace avenoir;
using nlohmann::json;

TEST_CASE("aller-retour JSON de chaque type de message", "[message_json]") {
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "Lance « firefox » s'il te plaît"},
        core::AssistantMessage{
            .text = "Je m'en occupe.",
            .tool_calls = {core::ToolCall{
                .id = "c1", .name = "launch_app", .arguments = json{{"name", "firefox"}}}},
            .usage = core::Usage{.input_tokens = 120, .output_tokens = 15, .cost_usd = 0.001}},
        core::AssistantMessage{.text = "Sans consommation.", .tool_calls = {}},
        core::ToolResultMessage{.call_id = "c1", .content = "lancée", .is_error = false},
        core::ToolResultMessage{.call_id = "c2", .content = "refusée", .is_error = true},
    };
    for (const auto& message : messages) {
        const auto back =
            storage::detail::message_from_json(storage::detail::message_to_json(message));
        REQUIRE(back.has_value());
        CHECK(*back == message);
    }
}

TEST_CASE("JSON enregistré illisible : erreur, pas de plantage", "[message_json]") {
    for (const json& bad : {json(nullptr), json::object(), json{{"role", "inconnu"}},
                            json{{"role", "user"}}, json{{"role", "assistant"}, {"text", "x"}},
                            json{{"role", "tool_result"}, {"call_id", "c"}}}) {
        CHECK_FALSE(storage::detail::message_from_json(bad).has_value());
    }
}

TEST_CASE("titre automatique", "[message_json]") {
    CHECK(storage::title_from("  Quelle   heure est-il ?\n") == "Quelle heure est-il ?");
    CHECK(storage::title_from("") == "Nouvelle conversation");
    const std::string title = storage::title_from("Peux-tu m'expliquer en détail la différence "
                                                  "entre TCP et UDP, avec des exemples concrets ?");
    CHECK(title.ends_with("…"));
    CHECK(title.size() <= 60 + std::string("…").size());
    // 'é' sur 2 octets : la coupe ne doit jamais tomber au milieu.
    const std::string accents = storage::title_from(std::string(70, 'a') + "éééé");
    CHECK(accents.ends_with("…"));
}
