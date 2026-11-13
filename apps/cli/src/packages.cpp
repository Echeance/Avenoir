#include "packages.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "avenoir/core/env.hpp"

namespace avenoir::cli {

namespace {

constexpr std::array<std::string_view, 1> kInstallable{"swaylock"};

struct PackageManager {
    std::string_view program;
    std::array<std::string_view, 2> install; ///< Arguments avant le nom du paquet.
};

// L'utilisateur confirme dans le gestionnaire lui-même (pas de « -y ») ;
// pacman ne réinstalle pas un paquet déjà présent (--needed).
constexpr std::array<PackageManager, 4> kManagers{{
    {.program = "pacman", .install = {"-S", "--needed"}},
    {.program = "apt-get", .install = {"install", ""}},
    {.program = "dnf", .install = {"install", ""}},
    {.program = "zypper", .install = {"install", ""}},
}};

} // namespace

bool installable_package(std::string_view package) {
    return std::ranges::contains(kInstallable, package);
}

std::optional<std::vector<std::string>>
install_command(std::string_view package, const std::function<bool(std::string_view)>& available) {
    if (!installable_package(package) || !available("sudo")) {
        return std::nullopt;
    }
    for (const auto& manager : kManagers) {
        if (!available(manager.program)) {
            continue;
        }
        std::vector<std::string> command{"sudo", std::string(manager.program)};
        for (const auto argument : manager.install) {
            if (!argument.empty()) {
                command.emplace_back(argument);
            }
        }
        command.emplace_back(package);
        return command;
    }
    return std::nullopt;
}

bool executable_in_path(std::string_view name) {
    const auto path = core::read_env("PATH");
    if (!path) {
        return false;
    }
#ifdef _WIN32
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif
    std::string_view rest = *path;
    while (!rest.empty()) {
        const auto end = rest.find(kSeparator);
        const std::string_view entry = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (entry.empty()) {
            continue; // dossier courant : jamais
        }
        std::error_code error;
        if (std::filesystem::is_regular_file(std::filesystem::path(entry) / name, error)) {
            return true;
        }
    }
    return false;
}

} // namespace avenoir::cli
