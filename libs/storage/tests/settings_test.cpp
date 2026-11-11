#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/storage/secrets.hpp"
#include "avenoir/storage/settings.hpp"

using namespace avenoir;
using Status = storage::SealReport::Status;
namespace fs = std::filesystem;

namespace {

/// Dossier temporaire effacé à la fin du test.
struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() /
                    ("avenoir-sceau-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { fs::create_directories(path); }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

core::AppConfig config_with_steps(std::size_t steps) {
    core::AppConfig config;
    config.agent.max_steps = steps;
    return config;
}

} // namespace

TEST_CASE("Réglages : écrits par Avenoir puis vérifiés", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings settings(directory.path / "avenoir" / "config.toml", secrets);

    auto report = settings.verify();
    REQUIRE(report.has_value());
    CHECK(report->status == Status::NoSettings);

    REQUIRE(settings.save(config_with_steps(4)).has_value());
    report = settings.verify();
    REQUIRE(report.has_value());
    CHECK(report->status == Status::Valid);
    CHECK(core::load_config(settings.config_path()).value().agent.max_steps == 4);
#ifndef _WIN32
    for (const auto& path : {settings.config_path(), settings.seal_path()}) {
        const auto perms = fs::status(path).permissions();
        CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
    }
#endif
}

TEST_CASE("Réglages : modification à la main refusée et restaurée", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings settings(directory.path / "config.toml", secrets);
    REQUIRE(settings.save(config_with_steps(4)).has_value());
    const std::string sealed = read(settings.config_path());

    write(settings.config_path(), sealed + "\n[permissions]\nsensitive = \"allow\"\n");
    const auto report = settings.verify();
    REQUIRE(report.has_value());
    CHECK(report->status == Status::Restored);
    CHECK(read(settings.config_path()) == sealed);
    CHECK(std::ranges::find(report->changes, "+ sensitive = \"allow\"") != report->changes.end());
    CHECK(settings.verify().value().status == Status::Valid);
}

TEST_CASE("Réglages : fichier supprimé, restauré", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings settings(directory.path / "config.toml", secrets);
    REQUIRE(settings.save(config_with_steps(4)).has_value());
    const std::string sealed = read(settings.config_path());
    fs::remove(settings.config_path());

    const auto report = settings.verify();
    REQUIRE(report.has_value());
    CHECK(report->status == Status::Restored);
    CHECK(read(settings.config_path()) == sealed);
}

TEST_CASE("Réglages : sceau supprimé ou falsifié, fichier mis de côté", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings settings(directory.path / "config.toml", secrets);
    REQUIRE(settings.save(config_with_steps(4)).has_value());

    SECTION("sceau supprimé") {
        fs::remove(settings.seal_path());
    }
    SECTION("sceau refait sans la clé") {
        write(settings.seal_path(),
              R"({"format":1,"hmac":"00","content":"[agent]\nmax_steps = 9\n"})");
    }
    SECTION("sceau illisible") {
        write(settings.seal_path(), "pas du JSON");
    }

    const auto report = settings.verify();
    REQUIRE(report.has_value());
    CHECK(report->status == Status::Rejected);
    CHECK_FALSE(fs::exists(settings.config_path()));
    CHECK(fs::exists(fs::path(settings.config_path().string() + ".rejected")));
    CHECK_FALSE(fs::exists(settings.seal_path()));
    // Ensuite : plus rien, la création guidée refera des réglages scellés.
    CHECK(settings.verify().value().status == Status::NoSettings);
}

TEST_CASE("Réglages : sceau d'une autre version", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings settings(directory.path / "config.toml", secrets);
    REQUIRE(settings.save(config_with_steps(4)).has_value());
    const std::string old_config = read(settings.config_path());
    const std::string old_seal = read(settings.seal_path());
    REQUIRE(settings.save(config_with_steps(7)).has_value());
    const std::string current = read(settings.config_path());

    SECTION("ancien fichier, sceau actuel : restauré") {
        write(settings.config_path(), old_config);
        const auto report = settings.verify();
        REQUIRE(report.has_value());
        CHECK(report->status == Status::Restored);
        CHECK(read(settings.config_path()) == current);
    }
    SECTION("ancien fichier et ancien sceau (rejeu) : refusé") {
        write(settings.config_path(), old_config);
        write(settings.seal_path(), old_seal);
        const auto report = settings.verify();
        REQUIRE(report.has_value());
        CHECK(report->status == Status::Rejected);
        CHECK(report->message.contains("rejeu"));
    }
}

TEST_CASE("Réglages : configuration existante adoptée", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings settings(directory.path / "config.toml", secrets);
    write(settings.config_path(), "[agent]\nmax_steps = 5\n");

    const auto report = settings.verify();
    REQUIRE(report.has_value());
    CHECK(report->status == Status::Adopted);
    CHECK(fs::exists(settings.seal_path()));
    CHECK(settings.verify().value().status == Status::Valid);

    // Une autre configuration a sa propre clé : sceller l'une ne touche pas
    // l'autre (un essai dans un dossier temporaire, par exemple).
    storage::SealedSettings other(directory.path / "autre.toml", secrets);
    write(other.config_path(), "[agent]\nmax_steps = 6\n");
    CHECK(other.verify().value().status == Status::Adopted);
    CHECK(settings.verify().value().status == Status::Valid);
}

TEST_CASE("Réglages : sceau d'une autre clé (autre fichier) refusé", "[settings]") {
    const TemporaryDirectory directory;
    storage::MemorySecretStore secrets;
    storage::SealedSettings first(directory.path / "a.toml", secrets);
    storage::SealedSettings second(directory.path / "b.toml", secrets);
    REQUIRE(first.save(config_with_steps(4)).has_value());
    REQUIRE(second.save(config_with_steps(8)).has_value());
    // Copier fichier et sceau de « a » sur « b » : la clé de « b » ne les valide pas.
    write(second.config_path(), read(first.config_path()));
    write(second.seal_path(), read(first.seal_path()));
    CHECK(second.verify().value().status == Status::Rejected);
}

TEST_CASE("changed_lines", "[settings]") {
    const auto changes =
        storage::changed_lines("[agent]\nmax_steps = 4\n", "[agent]\nmax_steps = 40\n\x1b[2J\n");
    // Caractère de contrôle remplacé : rien n'agit sur le terminal.
    CHECK(changes == std::vector<std::string>{"- max_steps = 4", "+  [2J", "+ max_steps = 40"});
}
