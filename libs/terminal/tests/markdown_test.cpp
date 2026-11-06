#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/terminal/markdown.hpp"
#include "avenoir/terminal/style.hpp"

using avenoir::terminal::render_markdown;
using Lines = std::vector<std::string>;

// Sans couleurs : on vérifie la structure (marqueurs retirés, puces…).
TEST_CASE("markdown : éléments en ligne", "[markdown]") {
    avenoir::terminal::set_enabled(false);
    CHECK(render_markdown("Il est **14 h 32**, *vendredi*.") == Lines{"Il est 14 h 32, vendredi."});
    CHECK(render_markdown("Lance `firefox` maintenant") == Lines{"Lance firefox maintenant"});
    CHECK(render_markdown("Voir [la doc](https://example.org).") ==
          Lines{"Voir la doc (https://example.org)."});
}

TEST_CASE("markdown : ce qui n'est pas du markdown reste intact", "[markdown]") {
    avenoir::terminal::set_enabled(false);
    CHECK(render_markdown("2 * 3 = 6") == Lines{"2 * 3 = 6"});
    CHECK(render_markdown("mon_fichier_de_config") == Lines{"mon_fichier_de_config"});
    CHECK(render_markdown("**non fermé") == Lines{"**non fermé"});
    CHECK(render_markdown("un ` seul") == Lines{"un ` seul"});
}

TEST_CASE("markdown : titres, listes, citations, règles", "[markdown]") {
    avenoir::terminal::set_enabled(false);
    CHECK(render_markdown("## Résumé\n- un\n* deux\n  + trois\n1. premier\n> citation\n---") ==
          Lines{"Résumé", "• un", "• deux", "  • trois", "1. premier", "▌ citation",
                "────────────────────"});
}

TEST_CASE("markdown : blocs de code non interprétés", "[markdown]") {
    avenoir::terminal::set_enabled(false);
    CHECK(render_markdown("Exemple :\n```cpp\nint **x = nullptr;\n```\nFin.") ==
          Lines{"Exemple :", "  int **x = nullptr;", "Fin."});
}

TEST_CASE("markdown : couleurs quand elles sont activées", "[markdown]") {
    avenoir::terminal::set_enabled(true);
    const auto lines = render_markdown("**gras**");
    avenoir::terminal::set_enabled(false);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "\x1b[1mgras\x1b[0m");
}
