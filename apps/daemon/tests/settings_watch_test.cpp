#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "settings_watch.hpp"

using namespace avenoir;
namespace fs = std::filesystem;

TEST_CASE("surveillance des réglages : l'état change quand un fichier change", "[reglages]") {
    const fs::path directory =
        fs::temp_directory_path() /
        ("avenoir-watch-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    const fs::path config = directory / "config.toml";
    const fs::path seal = directory / "config.toml.seal";

    // Rien encore : état stable, sans fichier.
    const auto empty = daemon::settings_stamp(config, seal);
    CHECK_FALSE(empty.config.has_value());
    CHECK(daemon::settings_stamp(config, seal) == empty);

    std::ofstream(config) << "[agent]\nmax_steps = 4\n";
    std::ofstream(seal) << "sceau";
    const auto written = daemon::settings_stamp(config, seal);
    CHECK(written != empty);
    CHECK(daemon::settings_stamp(config, seal) == written); // inchangé : même état

    // Modifié à la main (taille différente) : repéré.
    std::ofstream(config, std::ios::app) << "local_only = true\n";
    CHECK(daemon::settings_stamp(config, seal) != written);

    // Même taille, autre date : repéré aussi.
    const auto before = daemon::settings_stamp(config, seal);
    fs::last_write_time(config, fs::last_write_time(config) + std::chrono::seconds{2});
    CHECK(daemon::settings_stamp(config, seal) != before);

    // Sceau supprimé : repéré.
    const auto with_seal = daemon::settings_stamp(config, seal);
    fs::remove(seal);
    CHECK(daemon::settings_stamp(config, seal) != with_seal);

    std::error_code ignored;
    fs::remove_all(directory, ignored);
}
