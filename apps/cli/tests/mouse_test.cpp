#include <string>

#include <catch2/catch_test_macros.hpp>

#include "mouse.hpp"

using namespace avenoir::cli;

TEST_CASE("souris : séquences SGR du bouton gauche", "[mouse]") {
    REQUIRE(parse_sgr_mouse("\x1b[<0;12;5M").has_value());
    const auto press = parse_sgr_mouse("\x1b[<0;12;5M").value_or(MouseEvent{});
    CHECK(press.kind == MouseEvent::Kind::Press);
    CHECK(press.column == 12);
    CHECK(press.row == 5);
    CHECK(parse_sgr_mouse("\x1b[<32;13;5M").value_or(MouseEvent{}).kind == MouseEvent::Kind::Drag);
    CHECK(parse_sgr_mouse("\x1b[<0;13;5m").value_or(MouseEvent{}).kind ==
          MouseEvent::Kind::Release);
    // Maj tenue : toujours le bouton gauche.
    CHECK(parse_sgr_mouse("\x1b[<4;1;1M").has_value());
    // Autres boutons, molette, séquences mal formées : rien.
    CHECK_FALSE(parse_sgr_mouse("\x1b[<2;1;1M").has_value());
    CHECK_FALSE(parse_sgr_mouse("\x1b[<64;1;1M").has_value());
    CHECK_FALSE(parse_sgr_mouse("\x1b[<0;1M").has_value());
    CHECK_FALSE(parse_sgr_mouse("\x1b[<0;a;1M").has_value());
    CHECK_FALSE(parse_sgr_mouse("\x1b[<0;0;1M").has_value());
    CHECK_FALSE(parse_sgr_mouse("\x1b[A").has_value());
}

TEST_CASE("souris : découpe d'une ligne par colonnes", "[mouse]") {
    CHECK(columns_slice("bonjour le monde", 3, 7) == "jour");
    CHECK(columns_slice("élève", 1, 3) == "lè");
    CHECK(columns_slice("abc", 2, std::string::npos) == "c");
    CHECK(columns_slice("abc", 5, 9).empty());
}

TEST_CASE("souris : base64 pour OSC 52", "[mouse]") {
    CHECK(base64("").empty());
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("Avenoir é") == "QXZlbm9pciDDqQ==");
}
