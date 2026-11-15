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

TEST_CASE("adresses des onglets web : module et chemin", "[ui]") {
    const auto linux_form = ui::parse_module_address("avenoir-module://meteo/app.js?v=1#x");
    REQUIRE(linux_form.has_value());
    CHECK(linux_form.value_or(ui::ModuleAddress{}).module == "meteo");
    CHECK(linux_form.value_or(ui::ModuleAddress{}).path == "/app.js");
    const auto windows_form =
        ui::parse_module_address("https://ma-meteo.module.avenoir/index.html");
    REQUIRE(windows_form.has_value());
    CHECK(windows_form.value_or(ui::ModuleAddress{}).module == "ma-meteo");
    CHECK(windows_form.value_or(ui::ModuleAddress{}).path == "/index.html");
    CHECK(windows_form.value_or(ui::ModuleAddress{}).origin == "https://ma-meteo.module.avenoir");
    CHECK(linux_form.value_or(ui::ModuleAddress{}).origin == "avenoir-module://meteo");

    // Politique : l'origine du module seulement, aucun réseau.
    const std::string policy = ui::module_tab_policy("avenoir-module://meteo");
    CHECK(policy.contains("script-src avenoir-module://meteo;"));
    CHECK(policy.contains("connect-src 'none'"));
    CHECK(policy.starts_with("default-src 'none'"));
    CHECK(ui::parse_module_address("avenoir-module://meteo").value_or(ui::ModuleAddress{}).path ==
          "/");

    // Autres origines, identifiants invalides : refusés.
    CHECK_FALSE(ui::parse_module_address("https://app.avenoir/index.html").has_value());
    CHECK_FALSE(ui::parse_module_address("https://module.avenoir/index.html").has_value());
    CHECK_FALSE(ui::parse_module_address("https://a.b.module.avenoir/").has_value());
    CHECK_FALSE(ui::parse_module_address("https://x.module.avenoir.evil.com/").has_value());
    CHECK_FALSE(ui::parse_module_address("avenoir-module://Meteo/").has_value());
    CHECK_FALSE(ui::parse_module_address("avenoir-module://../etc/passwd").has_value());
    CHECK_FALSE(ui::parse_module_address("avenoir-module://-x/").has_value());
    CHECK_FALSE(ui::parse_module_address("http://meteo.module.avenoir/").has_value());
    CHECK_FALSE(ui::parse_module_address("file:///etc/passwd").has_value());
}
