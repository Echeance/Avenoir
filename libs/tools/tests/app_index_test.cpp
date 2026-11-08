#include <initializer_list>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/tools/tools.hpp"

#include "app_index.hpp"

using namespace avenoir::tools;
using namespace avenoir::tools::detail;

TEST_CASE("lecture d'un fichier .desktop", "[apps]") {
    const std::vector<std::string> french{"fr_FR", "fr"};
    const auto editor = parse_desktop_entry(R"([Desktop Entry]
Type=Application
Name=Text Editor
Name[fr]=Éditeur de texte
GenericName=Text Editor
GenericName[fr]=Éditeur de texte
Keywords=text;editor;
Keywords[fr]=texte;éditeur;bloc-notes;
Exec=/usr/bin/gnome-text-editor %U

[Desktop Action new-window]
Name=New Window
Exec=gnome-text-editor --new-window
)",
                                            "org.gnome.TextEditor", french);
    REQUIRE(editor.has_value());
    if (!editor) {
        return;
    }
    CHECK(editor.value().id == "org.gnome.TextEditor");
    CHECK(editor.value().name == "Éditeur de texte");
    CHECK(editor.value().aliases == std::vector<std::string>{"Éditeur de texte", "Text Editor",
                                                             "texte", "éditeur", "bloc-notes",
                                                             "gnome-text-editor"});

    // Système en anglais : nom anglais affiché, traductions françaises en alias.
    const auto browser = parse_desktop_entry(R"([Desktop Entry]
Type=Application
Name=Brave
GenericName=Web Browser
GenericName[fr]=Navigateur Web
Keywords=Internet;Browser;
Keywords[fr]=Internet;Navigateur;
Exec=brave %U
)",
                                             "brave-browser", {"en_US", "en"});
    REQUIRE(browser.has_value());
    if (!browser) {
        return;
    }
    CHECK(browser.value().name == "Brave");
    CHECK(match_score(browser.value(), "navigateur web") > 0);
    CHECK(match_score(browser.value(), "navigateur") > 0);

    // Pas une application à montrer.
    CHECK_FALSE(parse_desktop_entry("[Desktop Entry]\nType=Link\nName=X\n", "x", french));
    CHECK_FALSE(parse_desktop_entry("[Desktop Entry]\nType=Application\nName=X\nNoDisplay=true\n",
                                    "x", french));
    CHECK_FALSE(parse_desktop_entry("[Desktop Entry]\nType=Application\nName=X\nHidden=true\n", "x",
                                    french));
    CHECK_FALSE(parse_desktop_entry("[Desktop Entry]\nType=Application\n", "x", french));
    // Exécutable entre guillemets.
    const auto quoted = parse_desktop_entry(
        "[Desktop Entry]\nType=Application\nName=Outil\nExec=\"/opt/mon outil/outil\" --x\n", "o",
        french);
    REQUIRE(quoted.has_value());
    if (!quoted) {
        return;
    }
    CHECK(quoted.value().aliases.back() == "outil");
}

TEST_CASE("langues d'un .desktop d'après la locale", "[apps]") {
    CHECK(desktop_languages("fr_FR.UTF-8") == std::vector<std::string>{"fr_FR", "fr"});
    CHECK(desktop_languages("de_DE@euro") == std::vector<std::string>{"de_DE", "de"});
    CHECK(desktop_languages("en") == std::vector<std::string>{"en"});
    CHECK(desktop_languages("C.UTF-8").empty());
    CHECK(desktop_languages("").empty());
}

TEST_CASE("trouver l'application voulue", "[apps]") {
    const std::vector<AppEntry> apps{
        {.id = "notepad", .name = "Bloc-notes", .aliases = {"notepad"}},
        {.id = "calc", .name = "Calculatrice", .aliases = {"calc"}},
        {.id = "code", .name = "Visual Studio Code", .aliases = {"Éditeur de code", "code"}},
        {.id = "firefox", .name = "Firefox", .aliases = {"Navigateur web", "firefox"}},
        {.id = "brave-browser",
         .name = "Brave Web Browser",
         .aliases = {"Navigateur web", "brave"}},
    };
    CHECK(normalize_app_name("Bloc-Notes") == "blocnotes");
    CHECK(normalize_app_name("Éditeur de texte") == "editeurdetexte");

    const auto best = [&apps](const char* query) {
        const auto match = pick_app(apps, query);
        return match.best ? match.best->id : std::string("-");
    };
    CHECK(best("bloc-notes") == "notepad");
    CHECK(best("Bloc notes") == "notepad");
    CHECK(best("blocnotes") == "notepad");
    CHECK(best("notepad") == "notepad");
    CHECK(best("calc") == "calc");
    CHECK(best("calculatrice") == "calc");
    CHECK(best("code") == "code");
    CHECK(best("studio code") == "code");
    CHECK(best("visual") == "code");
    CHECK(best("photoshop") == "-");

    // Plusieurs navigateurs : pas de choix arbitraire, les candidats sont proposés.
    const auto ambiguous = pick_app(apps, "navigateur web");
    CHECK_FALSE(ambiguous.best.has_value());
    REQUIRE(ambiguous.candidates.size() == 2);

    const auto found = search_apps(apps, "navigateur", 10);
    REQUIRE(found.size() == 2);
    CHECK(search_apps(apps, "zzz", 10).empty());
}

TEST_CASE("nom d'application à chercher : accents oui, injections non", "[apps][validation]") {
    CHECK(validate_app_query("Bloc-notes") == "Bloc-notes");
    CHECK(validate_app_query("  Éditeur de texte ") == "Éditeur de texte");
    CHECK(validate_app_query("Outil Capture d'écran") == "Outil Capture d'écran");
    CHECK(validate_app_query("微信") == "微信");
    // Caractères piégés construits octet par octet : jamais en clair dans le source.
    const auto bytes = [](std::initializer_list<int> values) {
        std::string out;
        for (const int value : values) {
            out += static_cast<char>(value);
        }
        return out;
    };
    const std::vector<std::string> refused{
        "calc & del C:\\",
        "x; rm -rf ~",
        "$(reboot)",
        "`id`",
        "a|b",
        "--help",
        "../../bin/sh",
        "/usr/bin/firefox",
        "C:\\Windows\\cmd.exe",
        ".bashrc",
        "firefox\nreboot",
        "",
        "   ",
        "a\"b",
        "fire" + bytes({0xE2, 0x80, 0xAE}) + "xof", // marque bidirectionnelle U+202E
        "fire" + bytes({0xC2, 0x85}) + "fox",       // contrôle C1
        "bad" + bytes({0xFF}),                      // UTF-8 invalide
        "cut" + bytes({0xC3}),                      // UTF-8 tronqué
    };
    for (const auto& name : refused) {
        INFO("nom : " << name);
        CHECK_FALSE(validate_app_query(name).has_value());
    }
}
