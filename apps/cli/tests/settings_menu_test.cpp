#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/terminal/style.hpp"

#include "settings_menu.hpp"
#include "wolf.hpp"

using namespace avenoir;
using namespace avenoir::cli;

TEST_CASE("menu des réglages : valeurs à choisir ou à taper", "[settings]") {
    CHECK(setting_choices("oui | non") == std::vector<std::string>{"oui", "non"});
    CHECK(setting_choices("autoriser | confirmer | refuser").size() == 3);
    CHECK(setting_choices("auto | configured | both").size() == 3);
    // Nombre ou adresse : la valeur se tape.
    CHECK(setting_choices("1 à 100").empty());
    CHECK(setting_choices("http(s)://… | aucune").empty());
    CHECK(setting_choices("").empty());
    CHECK(setting_choices("seul").empty());
}

TEST_CASE("menu des réglages : familles et filtre", "[settings]") {
    CHECK(setting_family("agent.local_only") == "Assistant");
    CHECK(setting_family("permissions.sensitive") == "Permissions");
    CHECK(setting_family("modules.meteo.enabled") == "Modules");
    CHECK(setting_family("inconnu.x") == "inconnu");

    const std::vector<SettingEntry> entries = {
        {.key = "agent.local_only",
         .value = "oui",
         .accepted = "oui | non",
         .description = "aucun modèle distant",
         .security = true},
        {.key = "web.searxng_url",
         .value = "aucune",
         .accepted = "http(s)://… | aucune",
         .description = "votre instance SearXNG",
         .security = true},
        {.key = "orchestration.max_tasks",
         .value = "8",
         .accepted = "1 à 50",
         .description = "tâches au plus",
         .security = false},
    };
    CHECK(filter_settings(entries, "").size() == 3);
    CHECK(filter_settings(entries, "SEARX") == std::vector<std::size_t>{1});
    CHECK(filter_settings(entries, "équipe") == std::vector<std::size_t>{2}); // famille
    CHECK(filter_settings(entries, "EQUIPE") == std::vector<std::size_t>{2}); // sans accents
    CHECK(filter_settings(entries, "distant") == std::vector<std::size_t>{0});
    CHECK(filter_settings(entries, "introuvable").empty());
}

TEST_CASE("menu des réglages : réponse du démon", "[settings]") {
    const auto json = nlohmann::json::parse(R"([
        {"key": "agent.fast_path", "value": "oui", "accepted": "oui | non",
         "description": "voie rapide", "security": false},
        {"value": "sans clé"},
        42
    ])");
    const auto entries = settings_from_json(json);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].key == "agent.fast_path");
    CHECK(entries[0].accepted == "oui | non");
    CHECK(settings_from_json(nlohmann::json::object()).empty());
}

TEST_CASE("menu des réglages : panneau à la bonne taille, sélection visible", "[settings]") {
    terminal::set_enabled(false);
    MenuView view{.title = "Réglages",
                  .items = {},
                  .families = {},
                  .selected = 0,
                  .details = {"détail"},
                  .status = {},
                  .keys = "touches"};
    for (int i = 0; i < 40; ++i) {
        view.items.push_back("réglage " + std::to_string(i));
        view.families.emplace_back(i < 20 ? "A" : "B");
    }
    view.selected = 35;
    const auto lines = render_menu(view, 20, 60);
    CHECK(lines.size() == 20);
    bool selected_shown = false;
    for (const auto& line : lines) {
        selected_shown = selected_shown || line == " › réglage 35";
    }
    CHECK(selected_shown);
    CHECK(lines.back() == " touches");
    // Liste vide : un message plutôt qu'un panneau muet.
    view.items.clear();
    view.families.clear();
    const auto empty = render_menu(view, 10, 60);
    CHECK(empty.size() == 10);
}

TEST_CASE("loup : dimensions annoncées, avec ou sans couleurs", "[wolf]") {
    for (const bool colors : {true, false}) {
        terminal::set_enabled(colors);
        const auto art = wolf_art();
        CHECK(art.size() == 18);
        for (const auto& line : art) {
            CHECK(terminal::display_width(line) == kWolfArtWidth);
        }
        const auto icon = wolf_icon();
        CHECK(icon.size() == 15);
        for (const auto& line : icon) {
            CHECK(terminal::display_width(line) == kWolfIconWidth);
        }
    }
    // Sans couleurs : du texte simple, sans séquence de contrôle.
    for (const auto& line : wolf_art()) {
        CHECK(line.find('\x1b') == std::string::npos);
    }
}
