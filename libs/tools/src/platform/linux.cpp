// Code propre à Linux. Les processus sont lancés avec un tableau d'arguments
// (jamais de shell) ; `setsid -f` détache l'application lancée pour qu'elle
// ne soit ni tuée avec Avenoir ni laissée en processus zombie.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <reproc++/drain.hpp>
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

/// Lance `arguments`, attend la fin (au plus `timeout`) et renvoie le code de
/// sortie avec la sortie standard, ou rien si le programme est introuvable.
std::optional<std::pair<int, std::string>> run_capture(const std::vector<std::string>& arguments,
                                                       std::chrono::milliseconds timeout) {
    reproc::options options;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::discard;
    options.redirect.in.type = reproc::redirect::discard;
    options.deadline = std::chrono::duration_cast<reproc::milliseconds>(timeout);
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
    std::string output;
    reproc::sink::string sink(output);
    const std::error_code drained = reproc::drain(process, sink, reproc::sink::null);
    const auto [status, error] = process.stop(options.stop);
    if (error || drained) {
        return std::nullopt;
    }
    if (output.size() > 4096) {
        output.resize(4096); // sortie bornée : seules quelques lignes servent
    }
    return std::pair{status, std::move(output)};
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

/// Dossiers de données XDG : $XDG_DATA_HOME (ou ~/.local/share) d'abord,
/// puis $XDG_DATA_DIRS (ou /usr/local/share:/usr/share), puis Flatpak.
std::vector<std::filesystem::path> data_directories() {
    std::vector<std::filesystem::path> out;
    if (const auto home = core::read_env("XDG_DATA_HOME"); home && !home->empty()) {
        out.emplace_back(*home);
    } else if (const auto user = core::read_env("HOME")) {
        out.push_back(std::filesystem::path(*user) / ".local" / "share");
    }
    std::string_view dirs = "/usr/local/share:/usr/share";
    const auto configured = core::read_env("XDG_DATA_DIRS");
    if (configured && !configured->empty()) {
        dirs = *configured;
    }
    while (!dirs.empty()) {
        const auto colon = dirs.find(':');
        if (const auto dir = dirs.substr(0, colon); !dir.empty()) {
            out.emplace_back(dir);
        }
        dirs.remove_prefix(colon == std::string_view::npos ? dirs.size() : colon + 1);
    }
    if (const auto user = core::read_env("HOME")) {
        out.push_back(std::filesystem::path(*user) / ".local/share/flatpak/exports/share");
    }
    out.emplace_back("/var/lib/flatpak/exports/share");
    return out;
}

/// Contenu d'un petit fichier texte (64 Kio au plus), ou rien.
std::optional<std::string> read_small_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    std::string text(std::size_t{64} * 1024, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(stream.gcount()));
    return text;
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

std::vector<AppEntry> installed_apps() {
    std::string locale;
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (auto value = core::read_env(variable); value && !value->empty()) {
            locale = std::move(*value);
            break;
        }
    }
    const auto languages = tools::detail::desktop_languages(locale);
    std::vector<AppEntry> apps;
    std::vector<std::string> seen; // le premier dossier l'emporte (utilisateur d'abord)
    for (const auto& base : data_directories()) {
        const auto directory = base / "applications";
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 directory, std::filesystem::directory_options::skip_permission_denied, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (!it->is_regular_file(error) || it->path().extension() != ".desktop") {
                continue;
            }
            // Identifiant : chemin relatif, « / » remplacés par « - », sans extension.
            std::string id = std::filesystem::relative(it->path(), directory, error).string();
            id.resize(id.size() - std::string_view(".desktop").size());
            std::ranges::replace(id, '/', '-');
            if (std::ranges::contains(seen, id)) {
                continue;
            }
            seen.push_back(id);
            if (const auto text = read_small_file(it->path())) {
                if (auto app = tools::detail::parse_desktop_entry(*text, id, languages)) {
                    apps.push_back(std::move(*app));
                }
            }
        }
    }
    return apps;
}

std::expected<void, std::string> launch_entry(const AppEntry& app) {
    using namespace std::chrono_literals;
    if (const auto status = run_and_wait({"gtk-launch", app.id}, 10s); status == 0) {
        return {};
    }
    return std::unexpected("impossible de lancer « " + app.name +
                           " » (gtk-launch est-il installé ?)");
}

std::optional<AppEntry> default_browser() {
    // mimeapps.list : configuration de l'utilisateur d'abord, puis du système.
    std::vector<std::filesystem::path> files;
    if (const auto config = core::read_env("XDG_CONFIG_HOME"); config && !config->empty()) {
        files.push_back(std::filesystem::path(*config) / "mimeapps.list");
    } else if (const auto home = core::read_env("HOME")) {
        files.push_back(std::filesystem::path(*home) / ".config" / "mimeapps.list");
    }
    files.emplace_back("/etc/xdg/mimeapps.list");
    for (const auto& base : data_directories()) {
        files.push_back(base / "applications" / "mimeapps.list");
    }
    for (const auto& file : files) {
        const auto text = read_small_file(file);
        if (!text) {
            continue;
        }
        bool in_defaults = false;
        std::string_view rest = *text;
        while (!rest.empty()) {
            const auto end = rest.find('\n');
            const std::string_view line = rest.substr(0, end);
            rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
            if (line.starts_with('[')) {
                in_defaults = line.starts_with("[Default Applications]");
                continue;
            }
            if (!in_defaults || !(line.starts_with("x-scheme-handler/https=") ||
                                  line.starts_with("x-scheme-handler/http="))) {
                continue;
            }
            std::string_view value = line.substr(line.find('=') + 1);
            value = value.substr(0, value.find(';'));
            if (value.ends_with(".desktop")) {
                value.remove_suffix(std::string_view(".desktop").size());
            }
            for (auto& app : installed_apps()) {
                if (app.id == value) {
                    return app;
                }
            }
        }
    }
    return std::nullopt;
}

// ─── Contrôle système ───────────────────────────────────────────────────────

namespace {

std::string trimmed(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    return std::string(text);
}

/// Lance une commande courte ; erreur lisible si elle échoue.
std::expected<std::string, std::string> run_tool(const std::vector<std::string>& arguments,
                                                 std::string_view what) {
    using namespace std::chrono_literals;
    const auto result = run_capture(arguments, 5s);
    if (!result) {
        return std::unexpected(std::string(what) + " : « " + arguments.front() +
                               " » introuvable ou bloqué");
    }
    if (result->first != 0) {
        return std::unexpected(std::string(what) + " : échec de « " + arguments.front() + " »");
    }
    return trimmed(result->second);
}

/// Premier nombre décimal d'un texte (« Volume: 0.80 » → 0.80).
std::optional<double> first_number(std::string_view text) {
    const auto start = text.find_first_of("0123456789");
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t end = start;
    while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || text[end] == '.')) {
        ++end;
    }
    double value = 0;
    const auto number = text.substr(start, end - start);
    if (std::from_chars(std::to_address(number.begin()), std::to_address(number.end()), value).ec !=
        std::errc{}) {
        return std::nullopt;
    }
    return value;
}

bool has_backlight() {
    std::error_code error;
    return std::filesystem::exists("/sys/class/backlight", error) &&
           !std::filesystem::is_empty("/sys/class/backlight", error);
}

} // namespace

bool supports_volume() {
    return is_in_path("wpctl") || is_in_path("pactl");
}

std::expected<VolumeState, std::string> get_volume() {
    if (is_in_path("wpctl")) {
        // « Volume: 0.80 » ou « Volume: 0.80 [MUTED] » (PipeWire)
        auto text = run_tool({"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}, "volume");
        if (!text) {
            return std::unexpected(text.error());
        }
        const auto fraction = first_number(*text);
        if (!fraction) {
            return std::unexpected("volume illisible : « " + *text + " »");
        }
        return VolumeState{.level = static_cast<int>(std::lround(*fraction * 100)),
                           .muted = text->contains("MUTED")};
    }
    // PulseAudio : « Volume: front-left: 52428 /  80% / … »
    auto volume = run_tool({"pactl", "get-sink-volume", "@DEFAULT_SINK@"}, "volume");
    auto mute = run_tool({"pactl", "get-sink-mute", "@DEFAULT_SINK@"}, "volume");
    if (!volume || !mute) {
        return std::unexpected(volume ? mute.error() : volume.error());
    }
    const auto percent = volume->find('%');
    const auto slash = volume->rfind('/', percent);
    const auto level = percent != std::string::npos && slash != std::string::npos
                           ? first_number(std::string_view(*volume).substr(slash, percent - slash))
                           : std::nullopt;
    if (!level) {
        return std::unexpected("volume illisible : « " + *volume + " »");
    }
    return VolumeState{.level = static_cast<int>(*level), .muted = mute->contains("yes")};
}

std::expected<void, std::string> set_volume(int level) {
    if (is_in_path("wpctl")) {
        const auto value = std::format("{:.2f}", level / 100.0);
        if (auto done = run_tool({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", value}, "volume");
            !done) {
            return std::unexpected(done.error());
        }
        return {};
    }
    if (auto done = run_tool(
            {"pactl", "set-sink-volume", "@DEFAULT_SINK@", std::format("{}%", level)}, "volume");
        !done) {
        return std::unexpected(done.error());
    }
    return {};
}

std::expected<void, std::string> set_muted(bool muted) {
    const std::string flag = muted ? "1" : "0";
    auto done = is_in_path("wpctl")
                    ? run_tool({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", flag}, "son")
                    : run_tool({"pactl", "set-sink-mute", "@DEFAULT_SINK@", flag}, "son");
    if (!done) {
        return std::unexpected(done.error());
    }
    return {};
}

bool supports_media() {
    return is_in_path("playerctl");
}

std::expected<void, std::string> media_command(MediaCommand command) {
    std::string verb = "play-pause";
    switch (command) {
    case MediaCommand::PlayPause:
        break;
    case MediaCommand::Play:
        verb = "play";
        break;
    case MediaCommand::Pause:
        verb = "pause";
        break;
    case MediaCommand::Next:
        verb = "next";
        break;
    case MediaCommand::Previous:
        verb = "previous";
        break;
    case MediaCommand::Stop:
        verb = "stop";
        break;
    }
    if (!run_tool({"playerctl", verb}, "lecteur multimédia")) {
        return std::unexpected("aucun lecteur multimédia ne répond (rien en lecture ?)");
    }
    return {};
}

std::expected<std::string, std::string> now_playing() {
    auto text = run_tool({"playerctl", "metadata", "--format", "{{title}}\t{{artist}}\t{{status}}"},
                         "lecteur multimédia");
    if (!text || text->empty()) {
        return std::unexpected("aucun lecteur multimédia actif");
    }
    const auto first = text->find('\t');
    const auto second = text->find('\t', first + 1);
    if (first == std::string::npos || second == std::string::npos) {
        return std::unexpected("informations du lecteur illisibles");
    }
    const std::string title = text->substr(0, first);
    const std::string artist = text->substr(first + 1, second - first - 1);
    const std::string status = text->substr(second + 1);
    std::string out = title.empty() ? "(sans titre)" : "« " + title + " »";
    if (!artist.empty()) {
        out += " de " + artist;
    }
    if (status == "Playing") {
        out += " (en lecture)";
    } else if (status == "Paused") {
        out += " (en pause)";
    } else if (status == "Stopped") {
        out += " (arrêté)";
    }
    return out;
}

bool supports_brightness() {
    return is_in_path("brightnessctl") && has_backlight();
}

std::expected<int, std::string> get_brightness() {
    // « intel_backlight,backlight,500,50%,1000 »
    auto text = run_tool({"brightnessctl", "-m"}, "luminosité");
    if (!text) {
        return std::unexpected(text.error());
    }
    const auto percent = text->find('%');
    const auto comma = text->rfind(',', percent);
    const auto value = percent != std::string::npos && comma != std::string::npos
                           ? first_number(std::string_view(*text).substr(comma, percent - comma))
                           : std::nullopt;
    if (!value) {
        return std::unexpected("luminosité illisible : « " + *text + " »");
    }
    return static_cast<int>(*value);
}

std::expected<void, std::string> set_brightness(int level) {
    if (auto done = run_tool({"brightnessctl", "set", std::format("{}%", level)}, "luminosité");
        !done) {
        return std::unexpected(done.error());
    }
    return {};
}

bool supports_dark_mode() {
    using namespace std::chrono_literals;
    if (!is_in_path("gsettings")) {
        return false;
    }
    const auto result =
        run_capture({"gsettings", "get", "org.gnome.desktop.interface", "color-scheme"}, 3s);
    return result && result->first == 0;
}

std::expected<bool, std::string> get_dark_mode() {
    auto text = run_tool({"gsettings", "get", "org.gnome.desktop.interface", "color-scheme"},
                         "mode sombre");
    if (!text) {
        return std::unexpected(text.error());
    }
    return text->contains("dark");
}

std::expected<void, std::string> set_dark_mode(bool dark) {
    // Préférence lue par le portail XDG : applications GTK, Qt, navigateurs…
    if (auto done = run_tool({"gsettings", "set", "org.gnome.desktop.interface", "color-scheme",
                              dark ? "prefer-dark" : "default"},
                             "mode sombre");
        !done) {
        return std::unexpected(done.error());
    }
    return {};
}

bool supports_lock() {
    return is_in_path("loginctl");
}

std::expected<void, std::string> lock_screen() {
    if (auto done = run_tool({"loginctl", "lock-session"}, "verrouillage"); !done) {
        return std::unexpected(done.error());
    }
    return {};
}

bool supports_notify() {
    return is_in_path("notify-send");
}

std::expected<void, std::string> notify(const std::string& title, const std::string& body) {
    // « -- » : un texte commençant par « - » n'est jamais lu comme une option.
    if (auto done =
            run_tool({"notify-send", "--app-name=Avenoir", "--", title, body}, "notification");
        !done) {
        return std::unexpected(done.error());
    }
    return {};
}

bool supports_power() {
    return is_in_path("systemctl");
}

std::expected<void, std::string> power(PowerAction action) {
    std::string verb = "suspend";
    if (action == PowerAction::Shutdown) {
        verb = "poweroff";
    } else if (action == PowerAction::Reboot) {
        verb = "reboot";
    }
    if (auto done = run_tool({"systemctl", verb}, "alimentation"); !done) {
        return std::unexpected(done.error());
    }
    return {};
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
