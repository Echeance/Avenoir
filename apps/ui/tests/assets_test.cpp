#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "assets.hpp"

using namespace avenoir;
namespace fs = std::filesystem;

TEST_CASE("fichiers servis à la page : seulement ceux de l'interface", "[ui]") {
    const fs::path base =
        fs::temp_directory_path() /
        ("avenoir-ui-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path root = base / "ui";
    fs::create_directories(root / "assets");
    std::ofstream(root / "index.html") << "<!doctype html>";
    std::ofstream(root / "assets" / "index-1.js") << "1";
    std::ofstream(root / "assets" / "notes.txt") << "?";
    std::ofstream(base / "secret.html") << "secret";

    const auto index = ui::resolve_asset(root, "/");
    REQUIRE(index.has_value());
    CHECK(index.value_or(ui::Asset{}).mime == "text/html");
    CHECK(ui::resolve_asset(root, "/assets/index-1.js").value_or(ui::Asset{}).mime ==
          "text/javascript");

    // Hors du dossier, chemins piégés, type inconnu, absent : refusés.
    CHECK_FALSE(ui::resolve_asset(root, "/../secret.html").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "/assets/../../secret.html").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "/%2e%2e/secret.html").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "/assets\\index-1.js").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "assets/index-1.js").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "/assets/notes.txt").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "/absent.js").has_value());
    CHECK_FALSE(ui::resolve_asset(root, "/assets").has_value());

    // Lien symbolique qui sort du dossier : refusé.
    std::error_code error;
    fs::create_symlink(base / "secret.html", root / "lien.html", error);
    if (!error) {
        CHECK_FALSE(ui::resolve_asset(root, "/lien.html").has_value());
    }
    fs::remove_all(base, error);
}
