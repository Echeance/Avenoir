#include <string>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/modules/manifest.hpp"

using namespace avenoir;
using namespace avenoir::modules;

namespace {

constexpr const char* kMeteo = R"(
id = "meteo"
name = "Météo"
version = "1.0.0"
description = "Météo actuelle et prévisions"
run = ["avenoir-meteo", "--verbeux"]
permissions = ["network"]
env = ["METEO_LANGUE"]

[[tools]]
name = "forecast"
risk = "read_only"
public = true

[[tools]]
name = "set_city"
risk = "reversible"

[[tools]]
name = "secret"

[[commands]]
name = "meteo"
tool = "forecast"
arg = "city"
description = "météo d'une ville"

[[fast_path]]
pattern = '^météo à (.+)$'
tool = "forecast"
arg = "city"
arguments = { days = 1, details = false }

[[tabs]]
id = "today"
title = "Météo"
)";

core::Result<Manifest> parse(const std::string& text, const char* directory = "/m/meteo") {
    return parse_manifest(text, directory);
}

/// Le manifeste de référence, avec `before` remplacé par `after`.
std::string variant(const std::string& before, const std::string& after) {
    std::string text = kMeteo;
    const auto at = text.find(before);
    REQUIRE(at != std::string::npos);
    text.replace(at, before.size(), after);
    return text;
}

void expect_error(const std::string& text, const std::string& fragment,
                  const char* directory = "/m/meteo") {
    const auto manifest = parse(text, directory);
    REQUIRE_FALSE(manifest.has_value());
    INFO(manifest.error().message);
    CHECK(manifest.error().message.find(fragment) != std::string::npos);
}

} // namespace

TEST_CASE("manifeste complet", "[manifest]") {
    const auto manifest = parse(kMeteo);
    REQUIRE(manifest.has_value());
    CHECK(manifest->id == "meteo");
    CHECK(manifest->name == "Météo");
    CHECK(manifest->protocol == Protocol::Avenoir);
    CHECK(manifest->run.size() == 2);
    CHECK(manifest->permissions == std::vector<std::string>{"network"});
    CHECK(manifest->env == std::vector<std::string>{"METEO_LANGUE"});
    REQUIRE(manifest->tools.size() == 3);
    CHECK(manifest->tools[0].risk == core::RiskLevel::ReadOnly);
    CHECK(manifest->tools[0].is_public);
    CHECK_FALSE(manifest->tools[1].is_public);
    CHECK_FALSE(manifest->tools[2].risk.has_value()); // → sensitive à l'enregistrement
    REQUIRE(manifest->commands.size() == 1);
    CHECK(manifest->commands[0].arg == "city");
    REQUIRE(manifest->fast_path.size() == 1);
    CHECK(manifest->fast_path[0].arguments == nlohmann::json{{"days", 1}, {"details", false}});
    REQUIRE(manifest->tabs.size() == 1);
    CHECK(manifest->tabs[0].kind == TabKind::Declarative);
    CHECK(manifest->find_tool("set_city") != nullptr);
    CHECK(manifest->find_tool("absent") == nullptr);
}

TEST_CASE("manifeste minimal d'un serveur MCP", "[manifest]") {
    const auto manifest = parse(R"(
        id = "fichiers"
        name = "Fichiers"
        version = "0.3"
        run = ["npx", "-y", "serveur-fichiers"]
        protocol = "mcp"
    )",
                                "/m/fichiers");
    REQUIRE(manifest.has_value());
    CHECK(manifest->protocol == Protocol::Mcp);
    CHECK(manifest->tools.empty());
}

TEST_CASE("identifiants de module", "[manifest]") {
    CHECK(is_valid_module_id("meteo"));
    CHECK(is_valid_module_id("mon-module-2"));
    CHECK_FALSE(is_valid_module_id(""));
    CHECK_FALSE(is_valid_module_id("Meteo"));
    CHECK_FALSE(is_valid_module_id("-meteo"));
    CHECK_FALSE(is_valid_module_id("meteo-"));
    CHECK_FALSE(is_valid_module_id("me--teo"));
    CHECK_FALSE(is_valid_module_id("../meteo"));
    CHECK_FALSE(is_valid_module_id("mé-téo"));
    CHECK_FALSE(is_valid_module_id(std::string(33, 'a')));
    CHECK(qualified_tool_name("meteo", "forecast") == "meteo__forecast");
}

TEST_CASE("l'identifiant doit être le nom du dossier", "[manifest]") {
    expect_error(kMeteo, "nom du dossier", "/m/autre");
    expect_error(variant(R"(id = "meteo")", R"(id = "../meteo")"), "identifiant invalide",
                 "/m/../meteo");
}

TEST_CASE("le programme reste dans le dossier du module", "[manifest]") {
    expect_error(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = ["/bin/sh", "-c"])"),
                 "chemin absolu");
    expect_error(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = ["../../bin/sh"])"),
                 "sortir du dossier");
    expect_error(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = ["bin/../../x"])"),
                 "sortir du dossier");
    expect_error(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = [])"), "run");
    expect_error(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = "avenoir-meteo")"),
                 "tableau de chaînes");
#ifdef _WIN32
    expect_error(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = ['C:\x.exe'])"),
                 "chemin absolu");
#endif
    // Chemin relatif dans le dossier : accepté.
    CHECK(parse(variant(R"(run = ["avenoir-meteo", "--verbeux"])", R"(run = ["bin/meteo"])"))
              .has_value());
}

TEST_CASE("les commandes et règles ne visent que les outils du module", "[manifest]") {
    expect_error(variant(R"(tool = "forecast"
arg = "city"
description)",
                         R"(tool = "launch_app"
arg = "city"
description)"),
                 "n'est pas un outil déclaré");
    expect_error(variant(R"(tool = "forecast"
arg = "city"
arguments)",
                         R"(tool = "autre__forecast"
arg = "city"
arguments)"),
                 "n'est pas un outil déclaré");
}

TEST_CASE("les champs dangereux ou mal formés sont refusés", "[manifest]") {
    // Clés inconnues (fautes de frappe) à tous les niveaux.
    expect_error(variant(R"(version = "1.0.0")", "version = \"1.0.0\"\nrisque = 1"), "risque");
    expect_error(variant(R"(public = true)", "public = true\nrisk_level = \"read_only\""),
                 "tools[0].risk_level");
    // Noms d'outils.
    expect_error(variant(R"(name = "secret")", R"(name = "a b")"), "nom invalide");
    expect_error(variant(R"(name = "secret")", R"(name = "x__y")"), "nom invalide");
    expect_error(variant(R"(name = "secret")", R"(name = "forecast")"), "deux fois");
    expect_error(variant(R"(name = "secret")", "name = \"" + std::string(60, 'a') + "\""),
                 "nom invalide");
    expect_error(variant(R"(name = "secret")", "name = \"secret\"\nrisk = \"aucun\""),
                 "tools[2].risk");
    // Commandes.
    expect_error(variant(R"(name = "meteo")", R"(name = "/meteo")"), "commands[0].name");
    expect_error(variant(R"(arg = "city"
description)",
                         R"(arg = "ville; rm"
description)"),
                 "commands[0].arg");
    // Voie rapide.
    expect_error(variant(R"(pattern = '^météo à (.+)$')", R"(pattern = '^météo à (.+$')"),
                 "fast_path[0].pattern");
    expect_error(variant(R"(pattern = '^météo à (.+)$')", R"(pattern = '^météo$')"),
                 "groupe capturant");
    expect_error(
        variant(R"(arguments = { days = 1, details = false })", R"(arguments = { days = [1, 2] })"),
        "valeur simple");
    // Permissions, environnement, onglets.
    expect_error(variant(R"(permissions = ["network"])", R"(permissions = ["root"])"),
                 "permission inconnue");
    expect_error(variant(R"(env = ["METEO_LANGUE"])", R"(env = ["AVENOIR_SECRET"])"), "réservée");
    expect_error(variant(R"(env = ["METEO_LANGUE"])", R"(env = ["A=B"])"), "nom de variable");
    expect_error(variant(R"(title = "Météo")", "title = \"Météo\"\nkind = \"web\""), "ui.web");
    // Textes affichés : pas de séquence d'échappement du terminal.
    expect_error(variant(R"(name = "Météo")", R"(name = "Météo\u001b[2J")"), "name");
}

TEST_CASE("protocole et TOML illisible", "[manifest]") {
    expect_error(
        variant(R"(permissions = ["network"])", "permissions = [\"network\"]\nprotocol = \"grpc\""),
        "avenoir ou mcp");
    expect_error("id = ", "illisible");
}
