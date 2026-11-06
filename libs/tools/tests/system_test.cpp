#include <catch2/catch_test_macros.hpp>

#include "avenoir/tools/tools.hpp"

using avenoir::tools::detail::system_sentence;
using avenoir::tools::detail::SystemFacts;

TEST_CASE("phrase système complète", "[system]") {
    const SystemFacts facts{.os = "Linux",
                            .distribution = "CachyOS",
                            .arch = "x86_64",
                            .hostname = "cachyos",
                            .cpus = 16};
    CHECK(system_sentence(facts) ==
          "Système : CachyOS (Linux, x86_64), machine « cachyos », 16 cœurs.");
}

TEST_CASE("phrase système avec informations manquantes", "[system]") {
    const SystemFacts facts{.os = "Windows",
                            .distribution = std::nullopt,
                            .arch = "arm64",
                            .hostname = std::nullopt,
                            .cpus = 1};
    CHECK(system_sentence(facts) == "Système : Windows (arm64), 1 cœur.");
}
