// Paquets installables par la CLI : liste fermée, commande construite ici.

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "packages.hpp"

using namespace avenoir::cli;
using Command = std::vector<std::string>;

TEST_CASE("paquets : seulement ceux de la liste", "[packages]") {
    CHECK(installable_package("swaylock"));
    CHECK_FALSE(installable_package("bash"));
    CHECK_FALSE(installable_package("swaylock; rm -rf ~"));
    CHECK_FALSE(installable_package(""));
}

TEST_CASE("paquets : commande selon le gestionnaire", "[packages]") {
    const auto with = [](const std::vector<std::string_view>& present) {
        return [present](std::string_view name) { return std::ranges::contains(present, name); };
    };
    CHECK(install_command("swaylock", with({"sudo", "pacman"})) ==
          Command{"sudo", "pacman", "-S", "--needed", "swaylock"});
    CHECK(install_command("swaylock", with({"sudo", "apt-get"})) ==
          Command{"sudo", "apt-get", "install", "swaylock"});
    CHECK(install_command("swaylock", with({"sudo", "dnf"})) ==
          Command{"sudo", "dnf", "install", "swaylock"});
    CHECK(install_command("swaylock", with({"sudo", "zypper"})) ==
          Command{"sudo", "zypper", "install", "swaylock"});
    // Sans sudo, sans gestionnaire connu, ou paquet hors liste : rien.
    CHECK_FALSE(install_command("swaylock", with({"pacman"})).has_value());
    CHECK_FALSE(install_command("swaylock", with({"sudo", "emerge"})).has_value());
    CHECK_FALSE(install_command("bash", with({"sudo", "pacman"})).has_value());
}
