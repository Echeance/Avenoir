// Contrôle du système : règles de voie rapide et validation des arguments.
// Aucun test ne change le son, l'écran ni l'alimentation de la machine.

#include <initializer_list>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/router.hpp"
#include "avenoir/tools/tools.hpp"

#include "tools_impl.hpp"

using namespace avenoir;
using namespace avenoir::tools::detail;

namespace {

core::FastRouter system_router() {
    core::FastRouter router;
    REQUIRE(tools::register_system_rules(router));
    return router;
}

} // namespace

TEST_CASE("voie rapide : son et multimédia", "[system]") {
    const auto router = system_router();
    const auto route = [&router](const char* text) { return router.route(text); };

    REQUIRE(route("baisse le son") ==
            core::FastRoute{.tool = "set_volume", .arguments = {{"change", -10}}});
    REQUIRE(route("Monte le volume.") ==
            core::FastRoute{.tool = "set_volume", .arguments = {{"change", 10}}});
    REQUIRE(route("coupe le son") ==
            core::FastRoute{.tool = "set_volume", .arguments = {{"muted", true}}});
    REQUIRE(route("remets le son") ==
            core::FastRoute{.tool = "set_volume", .arguments = {{"muted", false}}});
    REQUIRE(route("volume à 30") ==
            core::FastRoute{.tool = "set_volume", .arguments = {{"level", "30"}}});
    REQUIRE(route("mets le son à 45 %") ==
            core::FastRoute{.tool = "set_volume", .arguments = {{"level", "45"}}});
    REQUIRE(route("pause") ==
            core::FastRoute{.tool = "media_control", .arguments = {{"action", "pause"}}});
    REQUIRE(route("morceau suivant") ==
            core::FastRoute{.tool = "media_control", .arguments = {{"action", "next"}}});
    REQUIRE(route("qu'est-ce qui passe ?") ==
            core::FastRoute{.tool = "media_control", .arguments = {{"action", "now_playing"}}});

    // Strict : une phrase plus riche part au LLM.
    CHECK_FALSE(route("baisse le son de la vidéo dans firefox"));
    CHECK_FALSE(route("pause café dans 10 minutes"));
    CHECK_FALSE(route("volume à 30 puis lance firefox"));
}

TEST_CASE("voie rapide : écran, session, alimentation", "[system]") {
    const auto router = system_router();
    const auto route = [&router](const char* text) { return router.route(text); };

    REQUIRE(route("luminosité à 60") ==
            core::FastRoute{.tool = "set_brightness", .arguments = {{"level", "60"}}});
    REQUIRE(route("baisse la luminosité") ==
            core::FastRoute{.tool = "set_brightness", .arguments = {{"change", -10}}});
    REQUIRE(route("active le mode sombre") ==
            core::FastRoute{.tool = "set_dark_mode", .arguments = {{"dark", true}}});
    REQUIRE(route("mode clair") ==
            core::FastRoute{.tool = "set_dark_mode", .arguments = {{"dark", false}}});
    REQUIRE(route("désactive le mode sombre") ==
            core::FastRoute{.tool = "set_dark_mode", .arguments = {{"dark", false}}});
    REQUIRE(route("verrouille l'ordinateur") == core::FastRoute{.tool = "lock_screen"});
    REQUIRE(route("mets l'ordinateur en veille") ==
            core::FastRoute{.tool = "power", .arguments = {{"action", "suspend"}}});

    // Éteindre ou redémarrer ne passe jamais par la voie rapide.
    CHECK_FALSE(route("éteins l'ordinateur"));
    CHECK_FALSE(route("redémarre le pc"));
}

TEST_CASE("lecture d'un pourcentage", "[system]") {
    using nlohmann::json;
    // Valeur lue (vide si absente) ; la lecture doit réussir.
    const auto read = [](const json& arguments, const char* key = "level") {
        const auto value =
            optional_int(arguments, key, key == std::string("level") ? 0 : -100, 100);
        REQUIRE(value.has_value());
        return value.value_or(std::nullopt);
    };
    const auto refused = [](const json& arguments) {
        return !optional_int(arguments, "level", 0, 100).has_value();
    };

    CHECK(read(json::object()) == std::nullopt);
    CHECK(read({{"level", 30}}) == 30);
    CHECK(read({{"level", "45"}}) == 45);
    CHECK(read({{"level", "45 %"}}) == 45);
    CHECK(read({{"level", 12.6}}) == 13);
    CHECK(read({{"change", "-10"}}, "change") == -10);

    CHECK(refused({{"level", 150}}));
    CHECK(refused({{"level", -1}}));
    CHECK(refused({{"level", "trente"}}));
    CHECK(refused({{"level", "30; rm -rf /"}}));
    CHECK(refused({{"level", "99999999999999999999"}}));
    CHECK(refused({{"level", true}}));
    CHECK(refused({{"level", json::array({30})}}));
}

TEST_CASE("texte d'une notification", "[system]") {
    REQUIRE(clean_notification("Réunion à 14 h", 100) == "Réunion à 14 h");
    REQUIRE(clean_notification("ligne 1\nligne 2\x1b[31m", 100) == "ligne 1 ligne 2 [31m");

    // Marques bidirectionnelles (U+202E) et invisibles (U+200B) retirées.
    // Octets assemblés un à un : un littéral contenant ces marques serait
    // lui-même trompeur à la lecture du code.
    const auto bytes = [](std::initializer_list<unsigned char> list) {
        std::string out;
        for (const unsigned char byte : list) {
            out += static_cast<char>(byte);
        }
        return out;
    };
    const std::string spoofed =
        "facture" + bytes({0xE2, 0x80, 0xAE}) + "fdp.exe" + bytes({0xE2, 0x80, 0x8B});
    REQUIRE(clean_notification(spoofed, 100) == "facturefdp.exe");

    // C1 (U+009B, introducteur CSI) remplacé.
    REQUIRE(clean_notification("a\xC2\x9B"
                               "b",
                               100) == "a b");

    // Coupe sans casser un caractère UTF-8 : « é » fait deux octets.
    REQUIRE(clean_notification("aé", 2) == "a…");
    REQUIRE(clean_notification("abcdef", 3) == "abc…");
}
