#include <algorithm>
#include <ranges>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/version.hpp"

TEST_CASE("la version suit le format majeur.mineur.correctif", "[version]") {
    const auto v = avenoir::core::version();

    REQUIRE_FALSE(v.empty());
    CHECK(std::ranges::count(v, '.') == 2);
    CHECK(std::ranges::all_of(v, [](char c) { return c == '.' || (c >= '0' && c <= '9'); }));
}
