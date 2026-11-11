// Phrases d'annonce des outils natifs : vraies réponses relevées au banc
// d'essai, et réponses qui ne doivent rien déclencher.

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/claims.hpp"
#include "avenoir/tools/tools.hpp"

using namespace avenoir;

namespace {

core::ToolCall call(std::string name) {
    return {.id = "1", .name = std::move(name), .arguments = nlohmann::json::object()};
}

} // namespace

TEST_CASE("annonces des outils natifs", "[claims]") {
    const auto check = core::make_claim_check(
        tools::action_claim_rules([](std::string_view /*name*/) { return true; }));
    const std::vector<core::ToolCall> none;

    // Ministral, cas `eteindre-refuse` (série 7) : il demande en texte au
    // lieu d'appeler `power`.
    CHECK(check("L'outil va éteindre l'ordinateur. Confirmez-vous cette action ?", none));
    CHECK(check("L'outil pour éteindre l'ordinateur est prêt. Confirmez-vous l'extinction de "
                "l'ordinateur ? (oui/non)",
                none));
    CHECK(check("J’ai lancé Firefox.", none));
    CHECK(check("C'est fait : j'ai baissé le son.", none));
    CHECK(check("Je mets en pause.", none));
    CHECK(check("Mode sombre activé !", none));
    // Ministral, cas `changer-moteur` (série 9).
    CHECK(check("Je vais utiliser **SearXNG** pour mes recherches web.", none));

    // Fait par l'outil (même refusé) : rien à redire.
    const std::vector power{call("power")};
    CHECK_FALSE(check("L'outil va éteindre l'ordinateur.", power));
    const std::vector opened{call("open_url")};
    CHECK_FALSE(check("J'ai ouvert la page.", opened));

    // Pas d'annonce d'action.
    CHECK_FALSE(check("Il est 14 h 05, samedi 7 novembre 2026.", none));
    CHECK_FALSE(check("Je ne peux pas éteindre l'ordinateur sans ton accord.", none));
    CHECK_FALSE(check("Je n'ai pas d'outil pour supprimer les fichiers temporaires.", none));
    CHECK_FALSE(check("Quelle application veux-tu que j'ouvre ?", none));
    CHECK_FALSE(check("J'ai réglé ta ville par défaut sur Arles.", none));
}

TEST_CASE("seulement les outils présents", "[claims]") {
    const auto rules =
        tools::action_claim_rules([](std::string_view name) { return name == "launch_app"; });
    REQUIRE(rules.size() == 1);
    CHECK(rules.front().tools == std::vector<std::string>{"launch_app"});
}
