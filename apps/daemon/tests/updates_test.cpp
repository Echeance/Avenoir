#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "updates.hpp"

using avenoir::daemon::is_newer;
using avenoir::daemon::latest_release;
using nlohmann::json;

TEST_CASE("mises à jour : comparaison des versions", "[daemon]") {
    CHECK(is_newer("0.3.0", "0.2.0"));
    CHECK(is_newer("v1.0.0", "0.9.9"));
    CHECK(is_newer("0.2.10", "0.2.9"));
    CHECK(is_newer("0.3.0", "0.3.0-rc.2"));
    CHECK(is_newer("0.3.0-beta.2", "0.3.0-beta.1"));
    CHECK(is_newer("0.3.0-rc.1", "0.3.0-beta.9"));
    CHECK_FALSE(is_newer("0.2.0", "0.2.0"));
    CHECK_FALSE(is_newer("0.1.9", "0.2.0"));
    CHECK_FALSE(is_newer("0.3.0-gamma.1", "0.2.0"));
    CHECK_FALSE(is_newer("pas une version", "0.2.0"));
    CHECK_FALSE(is_newer("0.3", "0.2.0"));
}

TEST_CASE("mises à jour : version publiée la plus récente", "[daemon]") {
    const json releases =
        json::array({{{"tag_name", "v0.4.0-beta.1"},
                      {"prerelease", true},
                      {"html_url", "https://github.com/a/b/4"}},
                     {{"tag_name", "v0.5.0"}, {"draft", true}},
                     {{"tag_name", "v0.3.0"}, {"html_url", "https://evil.example/x"}},
                     {{"tag_name", "nimporte"}},
                     "pas un objet"});
    const auto stable = latest_release(releases, false);
    REQUIRE(stable.has_value());
    CHECK(stable.value_or(avenoir::daemon::Release{}).version == "0.3.0");
    CHECK(stable.value_or(avenoir::daemon::Release{}).url.empty()); // pas une page GitHub
    const auto any = latest_release(releases, true);
    REQUIRE(any.has_value());
    CHECK(any.value_or(avenoir::daemon::Release{}).version == "0.4.0-beta.1");
    CHECK(any.value_or(avenoir::daemon::Release{}).prerelease);
    CHECK_FALSE(latest_release(json::object(), true).has_value());
}
