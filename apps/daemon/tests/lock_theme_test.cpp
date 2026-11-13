// Écran de verrouillage d'Avenoir pour swaylock : bureau, configuration, image.

#include <cstdint>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "lock_theme.hpp"

using namespace avenoir::daemon;

TEST_CASE("verrouillage : bureaux qui verrouillent eux-mêmes", "[lock]") {
    CHECK(desktop_locks_itself("GNOME"));
    CHECK(desktop_locks_itself("ubuntu:GNOME"));
    CHECK(desktop_locks_itself("KDE"));
    CHECK_FALSE(desktop_locks_itself("niri"));
    CHECK_FALSE(desktop_locks_itself("sway"));
    CHECK_FALSE(desktop_locks_itself(""));

    CHECK(lock_step_relevant("wayland", "niri"));
    CHECK(lock_step_relevant("wayland", "Hyprland"));
    CHECK_FALSE(lock_step_relevant("wayland", "KDE"));
    CHECK_FALSE(lock_step_relevant("x11", "i3"));
    CHECK_FALSE(lock_step_relevant("tty", ""));
}

TEST_CASE("verrouillage : configuration swaylock", "[lock]") {
    const std::string config = swaylock_config("/home/moi/.config/swaylock/avenoir.png");
    CHECK(config.starts_with(kLockConfigMarker));
    CHECK(config.contains("\nimage=/home/moi/.config/swaylock/avenoir.png\n"));
    CHECK(config.contains("\nscaling=center\n"));
    CHECK(config.contains("\ncolor=0b1020\n"));
    // Un « : » dans le chemin serait lu comme un nom d'écran.
    CHECK(swaylock_config("/home/a:b/x.png").contains("\nimage=:/home/a:b/x.png\n"));

    CHECK(lock_config_state(std::nullopt) == LockConfigState::Missing);
    CHECK(lock_config_state(config) == LockConfigState::Avenoir);
    CHECK(lock_config_state(std::string("color=000000\n")) == LockConfigState::Other);
}

TEST_CASE("verrouillage : image PNG du fond", "[lock]") {
    const std::string png = lock_image_png();
    REQUIRE(png.size() > 100);
    CHECK(png.starts_with(std::string("\x89PNG\r\n\x1a\n", 8)));
    // IHDR : largeur et hauteur, en gros-boutiste.
    const auto u32 = [&png](std::size_t at) {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value = (value << 8U) | static_cast<std::uint8_t>(png[at + i]);
        }
        return value;
    };
    CHECK(png.substr(12, 4) == "IHDR");
    CHECK(u32(16) == static_cast<std::uint32_t>(kLockImageWidth));
    CHECK(u32(20) == static_cast<std::uint32_t>(kLockImageHeight));
    CHECK(png.ends_with(std::string("IEND\xae\x42\x60\x82", 8)));
    CHECK(lock_image_png() == png); // toujours la même image
}
