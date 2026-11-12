#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/terminal/style.hpp"

using namespace avenoir::terminal;

TEST_CASE("paint : inchangé sans couleurs, séquences avec", "[style]") {
    set_enabled(false);
    CHECK(paint("texte", {.color = Color::Red, .bold = true}) == "texte");
    set_enabled(true);
    CHECK(paint("texte", {.color = Color::Red, .bold = true}) == "\x1b[1;31mtexte\x1b[0m");
    CHECK(paint("texte", {}) == "texte");
    set_enabled(false);
}

TEST_CASE("largeur affichée : UTF-8 et couleurs ignorées", "[style]") {
    CHECK(display_width("abc") == 3);
    CHECK(display_width("« été » ✓") == 9);
    CHECK(display_width("\x1b[1;31mrouge\x1b[0m") == 5);
    CHECK(pad("é", 3) == "é  ");
}

TEST_CASE("cadre : toutes les lignes ont la même largeur", "[style]") {
    set_enabled(true);
    const std::string framed =
        box("Titre", {"court", "une ligne un peu plus longue"}, Color::Yellow, 40);
    set_enabled(false);
    std::size_t start = 0;
    std::vector<std::size_t> widths;
    while (start < framed.size()) {
        auto end = framed.find('\n', start);
        if (end == std::string::npos) {
            end = framed.size();
        }
        widths.push_back(display_width(std::string_view(framed).substr(start, end - start)));
        start = end + 1;
    }
    REQUIRE(widths.size() == 4);
    for (const auto width : widths) {
        CHECK(width == 40);
    }
}

TEST_CASE("badges de risque", "[style]") {
    set_enabled(false);
    CHECK(risk_badge(avenoir::core::RiskLevel::ReadOnly) == "lecture seule");
    CHECK(risk_badge(avenoir::core::RiskLevel::Sensitive) == "SENSIBLE");
}

TEST_CASE("retour à la ligne aux espaces, mots trop longs coupés", "[style]") {
    CHECK(wrap("un deux trois quatre", 9) ==
          std::vector<std::string>{"un deux", "trois", "quatre"});
    CHECK(wrap("ligne1\nligne2", 20) == std::vector<std::string>{"ligne1", "ligne2"});
    CHECK(wrap("abcdefghij", 4) == std::vector<std::string>{"abcd", "efgh", "ij"});
    CHECK(wrap("éééééé", 4) == std::vector<std::string>{"éééé", "éé"});
}

TEST_CASE("raccourcir avec points de suspension", "[style]") {
    CHECK(ellipsize("court", 10) == "court");
    CHECK(ellipsize("une phrase trop longue", 10) == "une phras…");
    CHECK(display_width(ellipsize("ééééééééééé", 5)) == 5);
}

TEST_CASE("pixel art : demi-blocs, une ligne pour deux rangées de pixels", "[style]") {
    const std::vector<std::pair<char, Rgb>> palette = {{'K', Rgb{.r = 10, .g = 20, .b = 30}},
                                                       {'W', Rgb{.r = 250, .g = 250, .b = 250}}};
    set_enabled(false);
    CHECK(pixel_art({"KW", "WK"}, palette).empty());

    set_enabled(true);
    const auto lines = pixel_art({"K.W", "W..", "KK"}, palette);
    set_enabled(false);
    REQUIRE(lines.size() == 2); // 3 rangées de pixels → 2 lignes
    CHECK(display_width(lines[0]) == 3);
    CHECK(display_width(lines[1]) == 3);
    CHECK(lines[0].find("▀") != std::string::npos);
    CHECK(lines[1].find("▀") != std::string::npos);
}

TEST_CASE("strip_styles : couleurs retirées, texte gardé", "[style]") {
    set_enabled(true);
    const std::string colored = paint("Réglages", {.color = Color::Cyan, .bold = true}) + " · ok";
    CHECK(strip_styles(colored) == "Réglages · ok");
    CHECK(strip_styles("sans couleur") == "sans couleur");
    CHECK(strip_styles("\x1b[") == "");
    set_enabled(false);
}
