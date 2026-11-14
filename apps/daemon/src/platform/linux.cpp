// Démarrage à l'ouverture de session sous Linux : unité systemd utilisateur
// liée à la session graphique (le démon a ainsi l'environnement du bureau :
// notifications, ouverture d'applications…) ; sans systemd, entrée
// « autostart » XDG. `systemctl` est lancé par son chemin absolu avec un
// tableau d'arguments.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>
#include <spdlog/spdlog.h>

#include "avenoir/core/env.hpp"
#include "avenoir/core/error.hpp"

#include "../autostart.hpp"
#include "../gpu.hpp"

namespace avenoir::daemon {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kUnitName = "avenoird.service";
constexpr std::array<std::string_view, 2> kSystemctl{"/usr/bin/systemctl", "/bin/systemctl"};

std::unexpected<core::Error> autostart_error(std::string message) {
    return core::make_error(core::ErrorCode::Config,
                            "démarrage automatique : " + std::move(message));
}

std::optional<fs::path> config_home() {
    if (const auto xdg = core::read_env("XDG_CONFIG_HOME"); xdg && xdg->starts_with('/')) {
        return fs::path(*xdg);
    }
    if (const auto home = core::read_env("HOME"); home && home->starts_with('/')) {
        return fs::path(*home) / ".config";
    }
    return std::nullopt;
}

std::optional<std::string> systemctl() {
    for (const auto path : kSystemctl) {
        std::error_code error;
        if (fs::is_regular_file(path, error)) {
            return std::string(path);
        }
    }
    return std::nullopt;
}

/// Argument de ExecStart= / Exec= entre guillemets (échappements systemd et
/// XDG : « \\ » et « \" »).
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

/// Lance `systemctl --user <arguments>` et attend sa fin (borné).
core::Result<void> run_systemctl(const std::string& program, std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), {program, "--user"});
    reproc::options options;
    options.deadline = reproc::milliseconds(15000);
    options.redirect.in.type = reproc::redirect::discard;
    reproc::process process;
    if (const auto error = process.start(arguments, options)) {
        return autostart_error("systemctl impossible à lancer : " + error.message());
    }
    std::string out;
    std::string err;
    const auto drained =
        reproc::drain(process, reproc::sink::string(out), reproc::sink::string(err));
    const auto [status, error] = process.wait(reproc::milliseconds(5000));
    if (drained || error || status != 0) {
        const std::string line = err.substr(0, err.find('\n'));
        return autostart_error("systemctl a échoué" +
                               (line.empty() ? std::string{} : " : " + line));
    }
    return {};
}

core::Result<fs::path> current_executable() {
    std::error_code error;
    auto self = fs::read_symlink("/proc/self/exe", error);
    if (error) {
        return autostart_error("exécutable introuvable : " + error.message());
    }
    return self;
}

core::Result<void> write_file(const fs::path& path, const std::string& content) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return autostart_error("écriture impossible : " + path.string());
    }
    file << content;
    if (!file.flush()) {
        return autostart_error("écriture impossible : " + path.string());
    }
    return {};
}

} // namespace

core::Result<AutostartState> autostart_state() {
    const auto base = config_home();
    if (!base) {
        return autostart_error("dossier de configuration introuvable (HOME)");
    }
    std::error_code error;
    const fs::path unit = *base / "systemd" / "user" / kUnitName;
    const fs::path desktop = *base / "autostart" / "avenoird.desktop";
    if (systemctl()) {
        const fs::path link =
            *base / "systemd" / "user" / "graphical-session.target.wants" / kUnitName;
        return AutostartState{.enabled = fs::exists(unit, error) && fs::exists(link, error),
                              .where = unit.string()};
    }
    return AutostartState{.enabled = fs::exists(desktop, error), .where = desktop.string()};
}

core::Result<AutostartState> set_autostart(bool enabled) {
    const auto base = config_home();
    if (!base) {
        return autostart_error("dossier de configuration introuvable (HOME)");
    }
    auto program = current_executable();
    if (!program) {
        return std::unexpected(std::move(program.error()));
    }
    std::error_code error;
    if (const auto tool = systemctl()) {
        const fs::path unit = *base / "systemd" / "user" / kUnitName;
        if (enabled) {
            const std::string content =
                "# Écrit par Avenoir (/demarrage) : démon lancé avec la session graphique.\n"
                "[Unit]\n"
                "Description=Avenoir, assistant du système (démon)\n"
                "PartOf=graphical-session.target\n"
                "After=graphical-session.target\n\n"
                "[Service]\n"
                "ExecStart=" +
                quoted(program->string()) +
                "\n"
                "Restart=on-failure\n\n"
                "[Install]\n"
                "WantedBy=graphical-session.target\n";
            if (auto written = write_file(unit, content); !written) {
                return std::unexpected(std::move(written.error()));
            }
            if (auto done = run_systemctl(*tool, {"daemon-reload"}); !done) {
                return std::unexpected(std::move(done.error()));
            }
            if (auto done = run_systemctl(*tool, {"enable", std::string(kUnitName)}); !done) {
                return std::unexpected(std::move(done.error()));
            }
        } else {
            if (fs::exists(unit, error)) {
                // Échec sans conséquence : le fichier de l'unité est retiré quand même.
                if (auto done = run_systemctl(*tool, {"disable", std::string(kUnitName)}); !done) {
                    spdlog::warn("{}", core::describe(done.error()));
                }
                fs::remove(unit, error);
                if (auto done = run_systemctl(*tool, {"daemon-reload"}); !done) {
                    spdlog::warn("{}", core::describe(done.error()));
                }
            }
        }
        return autostart_state();
    }
    const fs::path desktop = *base / "autostart" / "avenoird.desktop";
    if (enabled) {
        const std::string content = "[Desktop Entry]\n"
                                    "Type=Application\n"
                                    "Name=Avenoir (démon)\n"
                                    "Comment=Écrit par Avenoir (/demarrage)\n"
                                    "Exec=" +
                                    quoted(program->string()) +
                                    "\n"
                                    "NoDisplay=true\n";
        if (auto written = write_file(desktop, content); !written) {
            return std::unexpected(std::move(written.error()));
        }
    } else {
        fs::remove(desktop, error);
    }
    return autostart_state();
}

std::optional<DemandingApp> demanding_app(std::uint64_t threshold_bytes) {
    // Mémoire vidéo de chaque programme de l'utilisateur, d'après le fdinfo
    // de ses descripteurs de carte graphique (/dev/dri) ; un client DRM ouvert
    // plusieurs fois n'est compté qu'une fois.
    std::optional<DemandingApp> biggest;
    std::error_code error;
    for (const auto& process : fs::directory_iterator("/proc", error)) {
        const std::string pid = process.path().filename().string();
        if (pid.empty() || !std::ranges::all_of(pid, [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        std::error_code ignored;
        std::vector<std::string> clients;
        std::uint64_t total = 0;
        for (const auto& descriptor : fs::directory_iterator(process.path() / "fd", ignored)) {
            std::error_code link_error;
            const auto target = fs::read_symlink(descriptor.path(), link_error);
            if (link_error || !target.string().starts_with("/dev/dri/")) {
                continue;
            }
            std::ifstream info(process.path() / "fdinfo" / descriptor.path().filename());
            const std::string text((std::istreambuf_iterator<char>(info)),
                                   std::istreambuf_iterator<char>());
            const auto usage = parse_drm_fdinfo(text);
            if (!usage || std::ranges::contains(clients, usage->client)) {
                continue;
            }
            clients.push_back(usage->client);
            total += usage->video_memory;
        }
        if (total < threshold_bytes || (biggest && biggest->video_memory >= total)) {
            continue;
        }
        std::ifstream comm(process.path() / "comm");
        std::string name;
        std::getline(comm, name);
        if (is_model_server(name)) {
            continue;
        }
        biggest = DemandingApp{.name = name, .video_memory = total};
    }
    return biggest;
}

} // namespace avenoir::daemon
