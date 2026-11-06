#include <algorithm>
#include <ranges>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/version.hpp"

TEST_CASE("la version suit le format SemVer", "[version]") {
    const std::string_view v = avenoir::core::version();
    REQUIRE_FALSE(v.empty());

    // « 1.2.0 » ou « 1.2.0-beta.1 » : le numéro s'arrête au premier tiret.
    const auto dash = v.find('-');
    const std::string_view number = v.substr(0, dash);

    CHECK(std::ranges::count(number, '.') == 2);
    CHECK(std::ranges::all_of(number, [](char c) { return c == '.' || (c >= '0' && c <= '9'); }));
    if (dash != std::string_view::npos) {
        CHECK(v.size() > dash + 1); // suffixe non vide après le tiret
    }
}
