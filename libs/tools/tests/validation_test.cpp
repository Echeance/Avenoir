#include <string>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/tools/tools.hpp"

using avenoir::tools::detail::validate_app_name;
using avenoir::tools::detail::validate_url;

TEST_CASE("noms d'application valides", "[validation]") {
    CHECK(validate_app_name("firefox") == "firefox");
    CHECK(validate_app_name("org.gnome.Nautilus") == "org.gnome.Nautilus");
    CHECK(validate_app_name("  code  ") == "code");
    CHECK(validate_app_name("Visual Studio Code") == "Visual Studio Code");
    CHECK(validate_app_name("g++") == "g++");
}

TEST_CASE("noms d'application refusés : injections, options, chemins", "[validation]") {
    for (const char* name : {
             "calc & del C:\\",      // séparateur de commande Windows
             "x; rm -rf ~",          // séparateur de commande shell
             "$(reboot)",            // substitution de commande
             "`id`",                 // substitution de commande
             "a|b",                  // tube
             "--help",               // option glissée
             "-e firefox",           // option glissée
             "../../bin/sh",         // chemin relatif
             "/usr/bin/firefox",     // chemin absolu
             "C:\\Windows\\cmd.exe", // chemin Windows
             ".bashrc",              // fichier caché
             "firefox\nreboot",      // retour à la ligne
             "",                     // vide
             "   ",                  // espaces seuls
         }) {
        INFO("nom : " << name);
        CHECK_FALSE(validate_app_name(name).has_value());
    }
    CHECK_FALSE(validate_app_name(std::string(101, 'a')).has_value());
}

TEST_CASE("URL valides", "[validation]") {
    CHECK(validate_url("https://example.org") == "https://example.org");
    CHECK(validate_url("http://localhost:8080/page?x=1&y=2#ancre") ==
          "http://localhost:8080/page?x=1&y=2#ancre");
    CHECK(validate_url(" https://fr.wikipedia.org/wiki/Manchot ") ==
          "https://fr.wikipedia.org/wiki/Manchot");
}

TEST_CASE("URL refusées : autres schémas, espaces, contrôle", "[validation]") {
    for (const char* url : {
             "javascript:alert(1)",
             "file:///etc/passwd",
             "ftp://example.org",
             "example.org",
             "https://",
             "https:///etc/passwd",
             "https://exa mple.org",
             "https://example.org/\nreboot",
             "https://example.org/\x7f",
             "",
         }) {
        INFO("url : " << url);
        CHECK_FALSE(validate_url(url).has_value());
    }
    CHECK_FALSE(validate_url("https://e.org/" + std::string(2048, 'a')).has_value());
}
