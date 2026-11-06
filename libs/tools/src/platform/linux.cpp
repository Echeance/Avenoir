// Code propre à Linux. Les processus sont lancés avec un tableau d'arguments
// (jamais de shell) ; `setsid -f` détache l'application lancée pour qu'elle
// ne soit ni tuée avec Avenoir ni laissée en processus zombie.

#include <array>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <reproc++/reproc.hpp>
#include <unistd.h>

#include "avenoir/core/env.hpp"

#include "platform.hpp"

namespace avenoir::tools::platform {

namespace {

/// Lance `arguments`, attend la fin (au plus `timeout`) et renvoie le code de
/// sortie, ou rien si le programme est introuvable ou n'a pas terminé.
std::optional<int> run_and_wait(const std::vector<std::string>& arguments,
                                std::chrono::milliseconds timeout) {
    reproc::options options;
    options.redirect.discard = true;
    options.stop = {
        .first = {.action = reproc::stop::wait,
                  .timeout = std::chrono::duration_cast<reproc::milliseconds>(timeout)},
        .second = {.action = reproc::stop::terminate, .timeout = reproc::milliseconds(1000)},
        .third = {.action = reproc::stop::kill, .timeout = reproc::milliseconds(1000)},
    };
    reproc::process process;
    if (process.start(arguments, options)) {
        return std::nullopt;
    }
    const auto [status, error] = process.stop(options.stop);
    if (error) {
        return std::nullopt;
    }
    return status;
}

/// Vrai si `name` est un exécutable présent dans un dossier du PATH.
bool is_in_path(const std::string& name) {
    const auto path = core::read_env("PATH");
    if (!path) {
        return false;
    }
    std::string_view rest = *path;
    while (!rest.empty()) {
        const auto colon = rest.find(':');
        const std::string_view directory = rest.substr(0, colon);
        rest = colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1);
        if (directory.empty()) {
            continue;
        }
        std::error_code error;
        const std::filesystem::path candidate = std::filesystem::path(directory) / name;
        const auto status = std::filesystem::status(candidate, error);
        constexpr auto kExecutable = std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec;
        if (!error && std::filesystem::is_regular_file(status) &&
            (status.permissions() & kExecutable) != std::filesystem::perms::none) {
            return true;
        }
    }
    return false;
}

std::string unquote(std::string_view value) {
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);
    }
    return std::string(value);
}

} // namespace

bool supports_launching() noexcept {
    return true;
}

std::expected<void, std::string> launch_app(const std::string& name) {
    using namespace std::chrono_literals;
    // 1. Application installée (fichier .desktop) : `gtk-launch` rend la main
    //    après le lancement ; code 0 = trouvée et lancée.
    if (const auto status = run_and_wait({"gtk-launch", name}, 10s); status == 0) {
        return {};
    }
    // 2. Exécutable du PATH. `setsid -f` rend la main immédiatement et ne
    //    signale pas un échec du lancement : on vérifie d'abord que
    //    l'exécutable existe.
    if (!is_in_path(name)) {
        return std::unexpected("application « " + name +
                               " » introuvable (ni application installée, ni exécutable du PATH)");
    }
    if (const auto status = run_and_wait({"setsid", "-f", name}, 5s); status == 0) {
        return {};
    }
    return std::unexpected("impossible de lancer « " + name + " »");
}

std::expected<void, std::string> open_url(const std::string& url) {
    using namespace std::chrono_literals;
    if (const auto status = run_and_wait({"setsid", "-f", "xdg-open", url}, 5s); status == 0) {
        return {};
    }
    return std::unexpected("impossible d'ouvrir l'adresse (xdg-open est-il installé ?)");
}

std::string_view os_name() noexcept {
    return "Linux";
}

std::optional<std::string> distribution() {
    for (const char* file : {"/etc/os-release", "/usr/lib/os-release"}) {
        std::ifstream stream(file);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.starts_with("PRETTY_NAME=")) {
                std::string value = unquote(std::string_view(line).substr(12));
                if (!value.empty()) {
                    return value;
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> hostname() {
    std::array<char, 256> buffer{};
    if (gethostname(buffer.data(), buffer.size() - 1) != 0) {
        return std::nullopt;
    }
    std::string name(buffer.data());
    return name.empty() ? std::nullopt : std::optional<std::string>(std::move(name));
}

} // namespace avenoir::tools::platform
