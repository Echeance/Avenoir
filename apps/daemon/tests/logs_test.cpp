#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "logs.hpp"

namespace fs = std::filesystem;

TEST_CASE("logs : fin du fichier, depuis un début de ligne, UTF-8 valide", "[daemon]") {
    const fs::path path = fs::temp_directory_path() / "avenoir-logs-test.log";
    {
        std::ofstream file(path, std::ios::binary);
        file << "première ligne\n";
        file << "deuxième é\n";
        file << "octets \xff\xfe invalides, \xc3 coupé\n";
        file << "fin\n";
    }
    const std::string all = avenoir::daemon::read_log_tail(path, 1000);
    CHECK(all.starts_with("première ligne\n"));
    CHECK(all.contains("octets  invalides,  coupé\n"));
    CHECK(all.ends_with("fin\n"));

    // Borné : on repart au début de la ligne suivante.
    const std::string tail = avenoir::daemon::read_log_tail(path, 10);
    CHECK(tail == "fin\n");

    CHECK(avenoir::daemon::read_log_tail(path.string() + ".absent", 100).empty());
    std::error_code error;
    fs::remove(path, error);
}
