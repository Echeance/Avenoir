#include <catch2/catch_test_macros.hpp>

#include "shortcut.hpp"

using namespace avenoir;

TEST_CASE("raccourci : commande par terminal", "[raccourci]") {
    const std::filesystem::path cli = "/opt/avenoir bin/avenoir";
    CHECK(daemon::quick_command("foot", cli).value_or("") ==
          "foot --title=Avenoir --window-size-chars=100x24 \"/opt/avenoir bin/avenoir\" --rapide");
    CHECK(daemon::quick_command("gnome-terminal", cli).value_or("").contains(" -- \""));
    CHECK_FALSE(daemon::quick_command("inconnu", cli).has_value());
}

TEST_CASE("raccourci : ligne du bureau", "[raccourci]") {
    const auto niri = daemon::desktop_binding("niri", "foot avenoir --rapide");
    REQUIRE(niri.has_value());
    CHECK(niri.value_or("").contains("Mod+Shift+A"));
    // Guillemets du chemin échappés dans la chaîne KDL.
    CHECK(daemon::desktop_binding("niri", "kitty \"/a b/avenoir\" --rapide")
              .value_or("")
              .contains("spawn-sh \"kitty \\\"/a b/avenoir\\\" --rapide\";"));
    CHECK(daemon::desktop_binding("sway", "x").value_or("").starts_with("bindsym $mod+Shift+a"));
    CHECK(daemon::desktop_binding("Hyprland", "x").value_or("").starts_with("bind = SUPER SHIFT"));
    CHECK_FALSE(daemon::desktop_binding("GNOME", "x").has_value());
}
