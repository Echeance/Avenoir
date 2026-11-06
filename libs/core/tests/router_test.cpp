#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/router.hpp"

using avenoir::core::FastRoute;
using avenoir::core::FastRouter;

TEST_CASE("les règles par défaut sont valides", "[router]") {
    CHECK(FastRouter::with_defaults().has_value());
}

TEST_CASE("lance une application", "[router]") {
    const auto router = FastRouter::with_defaults();
    REQUIRE(router.has_value());
    CHECK(router->route("Lance firefox.") ==
          FastRoute{.tool = "launch_app", .arguments = nlohmann::json{{"name", "firefox"}}});
    CHECK(router->route("  démarre org.gnome.Nautilus  ") ==
          FastRoute{.tool = "launch_app",
                    .arguments = nlohmann::json{{"name", "org.gnome.Nautilus"}}});
}

TEST_CASE("ouvre une URL", "[router]") {
    const auto router = FastRouter::with_defaults();
    REQUIRE(router.has_value());
    CHECK(router->route("ouvre https://example.org/page?x=1") ==
          FastRoute{.tool = "open_url",
                    .arguments = nlohmann::json{{"url", "https://example.org/page?x=1"}}});
}

TEST_CASE("demande l'heure", "[router]") {
    const auto router = FastRouter::with_defaults();
    REQUIRE(router.has_value());
    CHECK(router->route("Quelle heure est-il ?") ==
          FastRoute{.tool = "current_time", .arguments = nlohmann::json::object()});
}

TEST_CASE("une requête complexe part au LLM", "[router]") {
    const auto router = FastRouter::with_defaults();
    REQUIRE(router.has_value());
    CHECK_FALSE(router->route("ouvre le fichier de config et corrige-le").has_value());
    CHECK_FALSE(router->route("lance firefox; rm -rf ~").has_value());
    CHECK_FALSE(router->route("").has_value());
}

TEST_CASE("une règle invalide est refusée", "[router]") {
    FastRouter router;
    CHECK_FALSE(router.add_rule("(non fermée", "x").has_value());
}
