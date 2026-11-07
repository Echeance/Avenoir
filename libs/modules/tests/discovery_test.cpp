#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/modules/discovery.hpp"

using namespace avenoir;
using namespace avenoir::modules;

namespace {

/// Dossier temporaire supprimé à la fin du test.
class TempDir {
  public:
    TempDir() {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("avenoir-modules-" + std::to_string(random()));
        std::filesystem::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

void write_module(const std::filesystem::path& root, const std::string& directory,
                  const std::string& id, const std::string& name = "Module") {
    std::filesystem::create_directories(root / directory);
    std::ofstream(root / directory / "module.toml")
        << "id = \"" << id << "\"\nname = \"" << name << "\"\nversion = \"1\"\nrun = [\"x\"]\n";
}

} // namespace

TEST_CASE("découverte des modules", "[discovery]") {
    const TempDir temp;
    const auto bundled = temp.path() / "fournis";
    const auto user = temp.path() / "utilisateur";
    write_module(bundled, "meteo", "meteo", "Météo fournie");
    write_module(bundled, "agenda", "agenda");
    write_module(user, "meteo", "meteo", "Météo imitée"); // ne remplace pas la fournie
    write_module(user, "notes", "notes");
    write_module(user, "faux", "autre");                  // id ≠ dossier
    std::filesystem::create_directories(user / "vide");   // pas de module.toml
    std::filesystem::create_directories(user / ".cache"); // caché : ignoré

    const std::vector<SearchDirectory> directories{
        {.path = bundled, .origin = Origin::Bundled},
        {.path = user, .origin = Origin::User},
        {.path = temp.path() / "absent", .origin = Origin::User},
    };
    const Discovery found = discover(directories);

    REQUIRE(found.modules.size() == 3);
    CHECK(found.modules[0].manifest.id == "agenda"); // ordre alphabétique par dossier
    CHECK(found.modules[1].manifest.id == "meteo");
    CHECK(found.modules[1].manifest.name == "Météo fournie");
    CHECK(found.modules[1].origin == Origin::Bundled);
    CHECK(found.modules[2].manifest.id == "notes");
    CHECK(found.modules[2].origin == Origin::User);
    CHECK(found.modules[2].manifest.directory == user / "notes");

    REQUIRE(found.problems.size() == 3);
    const auto has_problem = [&](const std::string& directory, const std::string& fragment) {
        return std::ranges::any_of(found.problems, [&](const DiscoveryProblem& problem) {
            return problem.directory.filename() == directory && problem.message.contains(fragment);
        });
    };
    CHECK(has_problem("faux", "nom du dossier"));
    CHECK(has_problem("vide", "module.toml"));
    CHECK(has_problem("meteo", "déjà utilisé"));
}

TEST_CASE("activation par défaut selon la provenance", "[discovery]") {
    const DiscoveredModule bundled{.manifest = {}, .origin = Origin::Bundled};
    const DiscoveredModule user{.manifest = {}, .origin = Origin::User};
    CHECK(is_enabled(bundled, nullptr));
    CHECK_FALSE(is_enabled(user, nullptr)); // directive de sécurité n° 15

    const core::ModuleSettings unset;
    CHECK(is_enabled(bundled, &unset));
    CHECK_FALSE(is_enabled(user, &unset));

    const core::ModuleSettings on{.enabled = true, .risk = {}};
    const core::ModuleSettings off{.enabled = false, .risk = {}};
    CHECK(is_enabled(user, &on));
    CHECK_FALSE(is_enabled(bundled, &off));
}

TEST_CASE("dossiers de recherche par défaut", "[discovery]") {
    const auto directories = default_search_directories(std::filesystem::path("/données"));
    REQUIRE_FALSE(directories.empty());
    CHECK(directories.back().path == std::filesystem::path("/données") / "modules");
    CHECK(directories.back().origin == Origin::User);
#if defined(__linux__) || defined(_WIN32)
    REQUIRE(directories.size() == 2);
    CHECK(directories.front().origin == Origin::Bundled);
    CHECK(directories.front().path.filename() == "modules");
#endif
}
