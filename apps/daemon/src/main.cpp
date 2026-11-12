// Démon d'Avenoir (`avenoird`) : tourne en arrière-plan et sert ses
// interfaces (CLI aujourd'hui) par l'IPC locale (docs/PROJECT.md § 2.1,
// décision 53). La CLI le lance au besoin.
//
// Usage : avenoird [--config chemin] [--endpoint adresse] [--exit-when-idle]
//   --config          configuration de développement (non scellée)
//   --endpoint        adresse d'écoute (défaut : celle de l'utilisateur)
//   --exit-when-idle  s'arrêter quand le dernier client part
// Logs : logs/daemon.log du dossier de données (et stderr) ; niveau par
// AVENOIR_LOG. Journal d'audit : logs/audit.log.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "avenoir/core/env.hpp"
#include "avenoir/ipc/transport.hpp"
#include "avenoir/storage/store.hpp"
#include "avenoir/terminal/style.hpp"

#include "server.hpp"

namespace {

using namespace avenoir;

// Demande d'arrêt reçue par signal (seule écriture sûre dans un gestionnaire).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t g_stop_signal = 0;

extern "C" void avenoird_on_signal(int /*signal*/) {
    g_stop_signal = 1;
}

void report_fatal(const char* message) noexcept {
    (void)std::fputs("avenoird : ", stderr);
    (void)std::fputs(message, stderr);
    (void)std::fputs("\n", stderr);
}

/// Fichier tournant dans `logs/` du dossier de données (dossier `0700`).
std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> log_file(const std::filesystem::path& data,
                                                               const char* name) {
    constexpr std::size_t kMaxBytes = std::size_t{5} * 1024 * 1024;
    constexpr std::size_t kMaxFiles = 5;
    const std::filesystem::path directory = data / "logs";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    try {
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            (directory / name).string(), kMaxBytes, kMaxFiles);
        sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        return sink;
    } catch (const spdlog::spdlog_ex& e) {
        report_fatal((std::string("journal ") + name + " impossible : " + e.what()).c_str());
        return nullptr;
    }
}

/// Logs du démon (fichier + stderr) et journal d'audit (fichier toujours,
/// stderr avec AVENOIR_LOG).
void setup_logging() {
    const auto main_logger = spdlog::stderr_color_mt("avenoir");
    const auto audit = spdlog::stderr_color_mt("audit");
    spdlog::set_default_logger(main_logger);
    spdlog::set_pattern("%^[%n %l]%$ %v");
    auto level = spdlog::level::info;
    if (const auto wanted = core::read_env("AVENOIR_LOG")) {
        level = spdlog::level::from_str(*wanted);
    }
    main_logger->sinks().front()->set_level(spdlog::level::warn);
    audit->sinks().front()->set_level(core::read_env("AVENOIR_LOG") ? level : spdlog::level::off);
    if (const auto data = avenoir::storage::data_directory()) {
        if (auto file = log_file(*data, "daemon.log")) {
            file->set_level(level);
            main_logger->sinks().push_back(std::move(file));
        }
        if (auto file = log_file(*data, "audit.log")) {
            file->set_level(spdlog::level::info);
            audit->sinks().push_back(std::move(file));
        }
    }
    main_logger->set_level(spdlog::level::trace);
    main_logger->flush_on(spdlog::level::info);
    audit->set_level(spdlog::level::trace);
    audit->flush_on(spdlog::level::info);
}

int run(int argc, char** argv) {
    setup_logging();
    // Le démon peint ses textes ; chaque client retire les couleurs s'il n'en
    // veut pas (NO_COLOR, sortie redirigée).
    terminal::set_enabled(true);

    daemon::DaemonOptions options;
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--config" && i + 1 < args.size()) {
            options.config = std::filesystem::path(args[++i]);
        } else if (arg == "--endpoint" && i + 1 < args.size()) {
            options.endpoint.address = args[++i];
        } else if (arg == "--exit-when-idle") {
            options.exit_when_idle = true;
        } else {
            spdlog::error("argument inconnu : {} (avenoird [--config chemin] [--endpoint "
                          "adresse] [--exit-when-idle])",
                          arg);
            return EXIT_FAILURE;
        }
    }
    if (options.endpoint.address.empty()) {
        auto endpoint = ipc::default_endpoint();
        if (!endpoint) {
            spdlog::error("{}", core::describe(endpoint.error()));
            return EXIT_FAILURE;
        }
        options.endpoint = std::move(*endpoint);
    }

    if (std::signal(SIGINT, avenoird_on_signal) == SIG_ERR ||
        std::signal(SIGTERM, avenoird_on_signal) == SIG_ERR) {
        spdlog::warn("arrêt par signal indisponible");
    }
    std::stop_source stop;
    const std::jthread watcher([&stop](const std::stop_token& done) {
        while (!done.stop_requested()) {
            if (g_stop_signal != 0) {
                stop.request_stop();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    const int code = daemon::serve(options, stop.get_token());
    stop.request_stop();
    return code;
}

} // namespace

int main(int argc, char** argv) {
    // Seul endroit où les exceptions sont tolérées (docs/PROJECT.md § 4.2).
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        report_fatal(e.what());
    } catch (...) {
        report_fatal("exception inconnue");
    }
    return EXIT_FAILURE;
}
