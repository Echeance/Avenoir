#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/modules/discovery.hpp"
#include "avenoir/modules/install.hpp"

using namespace avenoir;
namespace fs = std::filesystem;

namespace {

class TempDir {
  public:
    TempDir() {
        std::random_device random;
        path_ = fs::temp_directory_path() / ("avenoir-install-" + std::to_string(random()));
        fs::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

  private:
    fs::path path_;
};

void write_module(const fs::path& directory, const std::string& id, const std::string& version,
                  const std::string& permissions = "") {
    fs::create_directories(directory);
    std::ofstream(directory / "module.toml")
        << "id = \"" << id << "\"\nname = \"Notes\"\nversion = \"" << version
        << "\"\nrun = [\"notes\"]\npermissions = [" << permissions << "]\n";
    std::ofstream(directory / "notes") << "programme " << version;
}

} // namespace

TEST_CASE("modules : installer, mettre à jour, désinstaller", "[install]") {
    const TempDir temp;
    const fs::path user = temp.path() / "modules";
    const fs::path data = temp.path() / "module-data";
    fs::create_directories(user);
    const std::vector<std::string> reserved{"meteo"};

    // Installation : rien n'est visible avant le redémarrage.
    write_module(temp.path() / "v1" / "notes", "notes", "1.0");
    auto plan = modules::prepare_install(temp.path() / "v1" / "notes", user, reserved);
    REQUIRE(plan.has_value());
    CHECK_FALSE(plan->installed.has_value());
    CHECK(plan->files == 2);
    REQUIRE(modules::stage_install(*plan, user).has_value());
    CHECK(modules::discover(std::vector{modules::SearchDirectory{.path = user}}).modules.empty());
    CHECK(modules::apply_pending(user, data) ==
          std::vector<std::string>{"module « notes » installé"});
    CHECK(modules::load_manifest(user / "notes").value_or(modules::Manifest{}).version == "1.0");

    // Mise à jour : nouvelle version, permissions en plus signalées.
    write_module(temp.path() / "v2" / "notes", "notes", "2.0", "\"network\"");
    plan = modules::prepare_install(temp.path() / "v2" / "notes", user, reserved);
    REQUIRE(plan.has_value());
    REQUIRE(plan->installed.has_value());
    CHECK(plan->installed.value_or(modules::Manifest{}).version == "1.0");
    CHECK(plan->added == std::vector<std::string>{"network"});
    REQUIRE(modules::stage_install(*plan, user).has_value());
    CHECK(modules::apply_pending(user, data).size() == 1);
    CHECK(modules::load_manifest(user / "notes").value_or(modules::Manifest{}).version == "2.0");

    // Désinstallation avec ses données.
    fs::create_directories(data / "notes");
    REQUIRE(modules::mark_uninstall(user, "notes", true).has_value());
    CHECK(fs::exists(user / "notes")); // pas avant le redémarrage
    CHECK(modules::apply_pending(user, data) ==
          std::vector<std::string>{"module « notes » désinstallé, ses données effacées"});
    CHECK_FALSE(fs::exists(user / "notes"));
    CHECK_FALSE(fs::exists(data / "notes"));
    CHECK(modules::apply_pending(user, data).empty());
}

TEST_CASE("modules : installations refusées", "[install]") {
    const TempDir temp;
    const fs::path user = temp.path() / "modules";
    fs::create_directories(user);
    const std::vector<std::string> reserved{"meteo"};

    // Module fourni : pas de remplacement.
    write_module(temp.path() / "a" / "meteo", "meteo", "9");
    CHECK_FALSE(modules::prepare_install(temp.path() / "a" / "meteo", user, reserved).has_value());
    // Dossier absent, manifeste absent, déjà dans les modules installés.
    CHECK_FALSE(modules::prepare_install(temp.path() / "absent", user, reserved).has_value());
    fs::create_directories(temp.path() / "vide");
    CHECK_FALSE(modules::prepare_install(temp.path() / "vide", user, reserved).has_value());
    write_module(user / "deja", "deja", "1");
    CHECK_FALSE(modules::prepare_install(user / "deja", user, reserved).has_value());
    // Lien symbolique dans le module (vers un secret, par exemple).
    write_module(temp.path() / "b" / "liens", "liens", "1");
    std::error_code error;
    fs::create_symlink(temp.path() / "secret", temp.path() / "b" / "liens" / "lien", error);
    if (!error) {
        CHECK_FALSE(
            modules::prepare_install(temp.path() / "b" / "liens", user, reserved).has_value());
    }
    // Désinstaller : identifiant invalide ou absent.
    CHECK_FALSE(modules::mark_uninstall(user, "../modules", false).has_value());
    CHECK_FALSE(modules::mark_uninstall(user, "absent", false).has_value());
}
