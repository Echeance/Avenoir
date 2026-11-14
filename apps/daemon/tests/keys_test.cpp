#include <catch2/catch_test_macros.hpp>

#include "runtime.hpp"

using namespace avenoir;

TEST_CASE("api_key_secret : nom du secret d'une clé d'API", "[cles]") {
    CHECK(daemon::api_key_secret("ANTHROPIC_API_KEY") == "api-key-anthropic-api-key");
    CHECK(daemon::api_key_secret("OPENAI_KEY2") == "api-key-openai-key2");
    // Refus : minuscules, séparateurs de chemin, espaces, vide, trop long.
    CHECK_FALSE(daemon::api_key_secret("anthropic_key").has_value());
    CHECK_FALSE(daemon::api_key_secret("../SECRET").has_value());
    CHECK_FALSE(daemon::api_key_secret("MA CLE").has_value());
    CHECK_FALSE(daemon::api_key_secret("").has_value());
    CHECK_FALSE(daemon::api_key_secret(std::string(65, 'A')).has_value());
}
