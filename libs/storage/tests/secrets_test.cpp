#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/storage/secrets.hpp"

using namespace avenoir;
namespace fs = std::filesystem;

namespace {

fs::path temporary_directory(const char* name) {
    return fs::temp_directory_path() /
           (std::string(name) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

} // namespace

TEST_CASE("is_valid_secret_name", "[secrets]") {
    CHECK(storage::is_valid_secret_name("settings-key-0a1b"));
    CHECK_FALSE(storage::is_valid_secret_name(""));
    CHECK_FALSE(storage::is_valid_secret_name("../cle"));
    CHECK_FALSE(storage::is_valid_secret_name("Cle"));
    CHECK_FALSE(storage::is_valid_secret_name("cle avec espace"));
    CHECK_FALSE(storage::is_valid_secret_name(std::string(101, 'a')));
}

TEST_CASE("FileSecretStore : lecture, écriture, droits", "[secrets]") {
    const fs::path directory = temporary_directory("avenoir-secrets");
    storage::FileSecretStore store(directory / "secrets");
    CHECK_FALSE(store.is_system_keychain());

    const auto missing = store.read("absent");
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->has_value());

    REQUIRE(store.write("cle", "0123abcd").has_value());
    CHECK(store.read("cle").value() == "0123abcd");
    REQUIRE(store.write("cle", "remplacee").has_value());
    CHECK(store.read("cle").value() == "remplacee");
#ifndef _WIN32
    const auto file = fs::status(directory / "secrets" / "cle").permissions();
    CHECK((file & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
    const auto folder = fs::status(directory / "secrets").permissions();
    CHECK((folder & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    // Noms et valeurs refusés : pas de sortie du dossier, pas de binaire.
    CHECK_FALSE(store.write("../evasion", "x").has_value());
    CHECK_FALSE(store.write("cle", "ligne\nsuivante").has_value());
    CHECK_FALSE(store.read("../evasion").has_value());

    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

TEST_CASE("MemorySecretStore", "[secrets]") {
    storage::MemorySecretStore store;
    CHECK_FALSE(store.read("x").value().has_value());
    REQUIRE(store.write("x", "valeur").has_value());
    CHECK(store.read("x").value() == "valeur");
}
