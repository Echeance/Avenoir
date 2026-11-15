#include "launcher.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace avenoir::cli {

namespace {

using namespace std::chrono_literals;

/// Délai pour qu'un démon lancé par la CLI réponde.
constexpr auto kDaemonStartup = 20s;

} // namespace

/// Adresse du démon : celle de l'utilisateur, ou un démon privé pour une
/// configuration donnée en argument (adresse propre à son chemin).
core::Result<ipc::Endpoint> daemon_endpoint(const std::optional<std::filesystem::path>& config) {
    auto base = ipc::default_endpoint();
    if (!base || !config) {
        return base;
    }
    std::error_code error;
    const auto absolute =
        std::filesystem::weakly_canonical(std::filesystem::absolute(*config), error);
    const auto hash = std::hash<std::string>{}(absolute.generic_string());
    return ipc::endpoint_variant(*base, std::format("dev-{:016x}", hash));
}

/// Se connecte au démon ; le lance s'il ne répond pas, puis attend qu'il soit prêt.
core::Result<std::unique_ptr<ipc::Connection>>
connect_daemon(const ipc::Endpoint& endpoint, const std::optional<std::filesystem::path>& config) {
    if (auto connection = ipc::connect(endpoint)) {
        return connection;
    }
    std::vector<std::string> arguments{"--endpoint", endpoint.address};
    if (config) {
        std::error_code error;
        arguments.emplace_back("--config");
        arguments.push_back(std::filesystem::absolute(*config, error).string());
        arguments.emplace_back("--exit-when-idle");
    }
    if (auto started = start_daemon(arguments); !started) {
        return std::unexpected(std::move(started.error()));
    }
    const auto deadline = std::chrono::steady_clock::now() + kDaemonStartup;
    while (true) {
        auto connection = ipc::connect(endpoint);
        if (connection || std::chrono::steady_clock::now() > deadline) {
            if (!connection) {
                return core::make_error(core::ErrorCode::Ipc,
                                        "le démon ne répond pas (voir logs/daemon.log du dossier "
                                        "de données)");
            }
            return connection;
        }
        std::this_thread::sleep_for(100ms);
    }
}

} // namespace avenoir::cli
