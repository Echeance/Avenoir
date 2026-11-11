// Actions annoncées mais non faites : règles génériques.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/claims.hpp"

using namespace avenoir::core;

TEST_CASE("réponse normalisée pour la recherche de phrases", "[claims]") {
    CHECK(normalize_reply("J’ai LANCÉ Firefox") == "j'ai lancé firefox");
    CHECK(normalize_reply("Éteint !") == "éteint !");
    CHECK(normalize_reply("Je passe à **SearXNG** (`searxng`)") == "je passe à searxng (searxng)");
}

TEST_CASE("règle : annonce sans l'outil, avec l'outil", "[claims]") {
    const auto check = make_claim_check({ClaimRule{
        .tools = {"lock_screen"}, .phrases = {"je verrouille"}, .action = "verrouiller"}});
    const std::vector<ToolCall> none;
    const std::vector<ToolCall> locked{
        ToolCall{.id = "1", .name = "lock_screen", .arguments = nlohmann::json::object()}};
    const std::vector<ToolCall> other{
        ToolCall{.id = "1", .name = "current_time", .arguments = nlohmann::json::object()}};

    const auto issue = check("Je verrouille la session.", none);
    REQUIRE(issue.has_value());
    if (issue) {
        CHECK(issue->reminder.contains("lock_screen"));
        CHECK(issue->warning.contains("verrouiller"));
    }
    CHECK(check("Je verrouille la session.", other).has_value());
    CHECK_FALSE(check("Je verrouille la session.", locked).has_value());
    CHECK_FALSE(check("Je ne peux pas verrouiller.", none).has_value());
}
