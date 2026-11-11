#include "avenoir/tools/searxng.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <random>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <cpr/cpr.h>
#include <spdlog/spdlog.h>

#include "platform.hpp"

namespace avenoir::tools {

namespace {

constexpr std::size_t kMaxOutput = std::size_t{16} * 1024;

std::string trim(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.remove_suffix(1);
    }
    while (!text.empty() && (text.front() == '\n' || text.front() == ' ')) {
        text.remove_prefix(1);
    }
    return std::string(text);
}

std::string random_secret() {
    static constexpr std::string_view kHex = "0123456789abcdef";
    std::random_device device;
    std::string secret;
    for (int i = 0; i < 64; ++i) {
        secret += kHex[device() % kHex.size()];
    }
    return secret;
}

/// Attend `duration` sauf si l'arrêt est demandé. Vrai si l'attente est allée au bout.
bool sleep_unless_stopped(std::chrono::milliseconds duration, const std::stop_token& stop) {
    constexpr auto kStep = std::chrono::milliseconds(50);
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        if (stop.stop_requested()) {
            return false;
        }
        std::this_thread::sleep_for(kStep);
    }
    return !stop.stop_requested();
}

} // namespace

CommandRunner default_command_runner() {
    return [](const std::vector<std::string>& arguments, std::chrono::seconds timeout,
              const std::stop_token& stop) -> core::Result<CommandResult> {
        auto done = platform::run_command(arguments, timeout, stop, kMaxOutput);
        if (!done) {
            return done.error().cancelled
                       ? core::make_error(core::ErrorCode::Cancelled, {})
                       : core::make_error(core::ErrorCode::Config, done.error().message);
        }
        return CommandResult{.exit_code = done->exit_code, .output = std::move(done->output)};
    };
}

SearxngHost::SearxngHost(SearxngOptions options) : options_(std::move(options)) {
    if (!options_.run) {
        options_.run = default_command_runner();
    }
    if (!options_.ready) {
        options_.ready = [](const std::string& base_url) {
            const cpr::Response response =
                cpr::Get(cpr::Url{base_url + "/healthz"}, cpr::Timeout{std::chrono::seconds{2}});
            return response.error.code == cpr::ErrorCode::OK && response.status_code == 200;
        };
    }
    if (options_.idle_check.count() > 0) {
        watcher_ = std::jthread([this](const std::stop_token& stop) {
            std::unique_lock lock(mutex_);
            while (!stop.stop_requested()) {
                wake_.wait_for(lock, stop, options_.idle_check, [] { return false; });
                if (stop.stop_requested()) {
                    break;
                }
                lock.unlock();
                stop_if_idle(std::chrono::steady_clock::now());
                lock.lock();
            }
        });
    }
}

SearxngHost::~SearxngHost() {
    watcher_.request_stop();
    if (watcher_.joinable()) {
        watcher_.join();
    }
    stop();
}

bool SearxngHost::running() const {
    const std::scoped_lock lock(mutex_);
    return !base_url_.empty();
}

core::Result<std::string> SearxngHost::ensure_started(const std::stop_token& stop) {
    const std::scoped_lock lock(mutex_);
    if (base_url_.empty()) {
        auto started = start_locked(stop);
        if (!started) {
            return started;
        }
        base_url_ = std::move(*started);
    }
    last_used_ = std::chrono::steady_clock::now();
    return base_url_;
}

void SearxngHost::stop() {
    const std::scoped_lock lock(mutex_);
    stop_locked();
}

bool SearxngHost::stop_if_idle(std::chrono::steady_clock::time_point now) {
    const std::scoped_lock lock(mutex_);
    if (base_url_.empty() || now - last_used_ < options_.idle) {
        return false;
    }
    spdlog::info("SearXNG inactif depuis {} min : arrêt", options_.idle.count());
    stop_locked();
    return true;
}

void SearxngHost::stop_locked() {
    if (base_url_.empty()) {
        return;
    }
    base_url_.clear();
    // Lancé avec --rm : arrêté, le conteneur est supprimé.
    const auto stopped = options_.run({runtime_, "stop", "--time", "2", std::string(kContainer)},
                                      std::chrono::seconds{20}, {});
    if (!stopped || stopped->exit_code != 0) {
        spdlog::warn("arrêt de SearXNG impossible");
    }
}

core::Result<void> SearxngHost::write_settings() const {
    const auto path = options_.config_dir / "settings.yml";
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        return {};
    }
    std::filesystem::create_directories(options_.config_dir, error);
    if (error) {
        return core::make_error(core::ErrorCode::Config,
                                "dossier de SearXNG impossible à créer : " + error.message());
    }
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) {
            return core::make_error(core::ErrorCode::Config,
                                    "configuration de SearXNG impossible à écrire");
        }
        file << detail::searxng_settings(random_secret());
    }
    // Lisible par l'utilisateur du conteneur (autre identifiant sur la
    // machine) ; la clé ne protège que les sessions de cette instance locale.
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read | std::filesystem::perms::others_read,
        error);
    return {};
}

core::Result<std::string> SearxngHost::start_locked(const std::stop_token& stop) {
    const auto run = [this, &stop](std::vector<std::string> arguments,
                                   std::chrono::seconds timeout) {
        arguments.insert(arguments.begin(), runtime_);
        return options_.run(arguments, timeout, stop);
    };
    const auto ok = [](const core::Result<CommandResult>& result) {
        return result && result->exit_code == 0;
    };
    // Arrêt sans tenir compte de Ctrl+C : le conteneur ne doit pas rester.
    const auto stop_container = [this](const std::string& name) {
        const auto stopped =
            options_.run({runtime_, "stop", "--time", "2", name}, std::chrono::seconds{20}, {});
        if (!stopped || stopped->exit_code != 0) {
            spdlog::warn("arrêt de SearXNG impossible");
        }
    };

    // 1. podman, sinon docker.
    if (runtime_.empty()) {
        for (const auto& candidate : options_.runtimes) {
            if (ok(options_.run({candidate, "--version"}, std::chrono::seconds{10}, stop))) {
                runtime_ = candidate;
                break;
            }
        }
        if (runtime_.empty()) {
            return core::make_error(core::ErrorCode::Config,
                                    "ni podman ni docker n'est installé : SearXNG ne peut pas "
                                    "être lancé en local");
        }
    }
    if (auto written = write_settings(); !written) {
        return std::unexpected(std::move(written.error()));
    }

    // 2. Image : téléchargée la première fois.
    const std::string image(kImage);
    if (!ok(run({"image", "inspect", image}, std::chrono::seconds{30}))) {
        spdlog::info("téléchargement de {}", image);
        if (options_.notify) {
            options_.notify(std::format("Première recherche : téléchargement de SearXNG ({}, "
                                        "environ 200 Mo) avec {}…",
                                        image, runtime_));
        }
        if (!ok(run({"pull", image}, std::chrono::seconds{900}))) {
            if (stop.stop_requested()) {
                return core::make_error(core::ErrorCode::Cancelled, {});
            }
            return core::make_error(core::ErrorCode::Config,
                                    "téléchargement de l'image SearXNG impossible");
        }
    }

    // 3. Une instance laissée par un lancement précédent est reprise si elle
    //    répond, sinon supprimée.
    const std::string container(kContainer);
    if (const auto port = run({"port", container, "8080"}, std::chrono::seconds{10}); ok(port)) {
        if (const auto base = detail::searxng_base_url(port->output);
            base && options_.ready(*base)) {
            spdlog::info("SearXNG déjà lancé : {}", *base);
            return *base;
        }
    }
    if (const auto removed = run({"rm", "--force", container}, std::chrono::seconds{30});
        !ok(removed)) {
        spdlog::debug("pas d'ancien conteneur SearXNG à supprimer");
    }

    // 4. Lancement : boucle locale seulement, port choisi par le système.
    // Seul le fichier est monté, en lecture seule : monté comme dossier, le
    // conteneur se l'approprierait (propriétaire changé sur la machine).
    const std::string volume =
        (options_.config_dir / "settings.yml").string() + ":/etc/searxng/settings.yml:ro,Z";
    const auto launched = run({"run", "--detach", "--rm", "--name", container, "--publish",
                               "127.0.0.1::8080", "--volume", volume, image},
                              std::chrono::seconds{60});
    if (!ok(launched)) {
        return core::make_error(core::ErrorCode::Config, "lancement de SearXNG impossible");
    }
    const auto port = run({"port", container, "8080"}, std::chrono::seconds{10});
    const auto base = ok(port) ? detail::searxng_base_url(port->output) : std::nullopt;
    if (!base) {
        stop_container(container);
        return core::make_error(core::ErrorCode::Config, "adresse de SearXNG introuvable");
    }

    // 5. Attente : SearXNG répond en quelques secondes.
    const auto deadline = std::chrono::steady_clock::now() + options_.ready_timeout;
    while (!options_.ready(*base)) {
        if (std::chrono::steady_clock::now() >= deadline ||
            !sleep_unless_stopped(options_.poll_interval, stop)) {
            stop_container(container);
            return stop.stop_requested()
                       ? core::make_error(core::ErrorCode::Cancelled, {})
                       : core::make_error(core::ErrorCode::Config,
                                          "SearXNG ne répond pas après son lancement");
        }
    }
    spdlog::info("SearXNG lancé : {}", *base);
    return *base;
}

namespace detail {

std::optional<std::string> searxng_base_url(std::string_view port_output) {
    // Une ligne par adresse publiée ; seule la boucle locale IPv4 convient.
    while (!port_output.empty()) {
        const auto end = port_output.find('\n');
        const std::string line = trim(port_output.substr(0, end));
        port_output =
            end == std::string_view::npos ? std::string_view{} : port_output.substr(end + 1);
        constexpr std::string_view kLoopback = "127.0.0.1:";
        if (!line.starts_with(kLoopback)) {
            continue;
        }
        const std::string_view port = std::string_view(line).substr(kLoopback.size());
        if (port.empty() || port.size() > 5 ||
            !std::ranges::all_of(port, [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        return "http://" + line;
    }
    return std::nullopt;
}

std::string searxng_settings(std::string_view secret_key) {
    return std::format(R"(# Écrit par Avenoir : instance locale de SearXNG pour la recherche web.
use_default_settings: true

server:
  secret_key: "{}"
  # Accessible seulement depuis cette machine (port publié sur 127.0.0.1).
  limiter: false
  image_proxy: false
  public_instance: false

search:
  formats:
    - html
    - json
)",
                       secret_key);
}

} // namespace detail

} // namespace avenoir::tools
