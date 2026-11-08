// Repli pour les plateformes non prises en charge : les outils qui en
// dépendent ne sont pas enregistrés (supports_launching() renvoie faux).

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform.hpp"

namespace avenoir::tools::platform {

bool supports_launching() noexcept {
    return false;
}

std::expected<void, std::string> launch_app(const std::string& /*name*/) {
    return std::unexpected("action non prise en charge sur cette plateforme");
}

std::expected<void, std::string> open_url(const std::string& /*url*/) {
    return std::unexpected("action non prise en charge sur cette plateforme");
}

std::vector<AppEntry> installed_apps() {
    return {};
}

std::expected<void, std::string> launch_entry(const AppEntry& /*app*/) {
    return std::unexpected("action non prise en charge sur cette plateforme");
}

std::optional<AppEntry> default_browser() {
    return std::nullopt;
}

namespace {
std::unexpected<std::string> unsupported() {
    return std::unexpected<std::string>("action non prise en charge sur cette plateforme");
}
} // namespace

bool supports_volume() {
    return false;
}
std::expected<VolumeState, std::string> get_volume() {
    return unsupported();
}
std::expected<void, std::string> set_volume(int /*level*/) {
    return unsupported();
}
std::expected<void, std::string> set_muted(bool /*muted*/) {
    return unsupported();
}
bool supports_media() {
    return false;
}
std::expected<void, std::string> media_command(MediaCommand /*command*/) {
    return unsupported();
}
std::expected<std::string, std::string> now_playing() {
    return unsupported();
}
bool supports_brightness() {
    return false;
}
std::expected<int, std::string> get_brightness() {
    return unsupported();
}
std::expected<void, std::string> set_brightness(int /*level*/) {
    return unsupported();
}
bool supports_dark_mode() {
    return false;
}
std::expected<bool, std::string> get_dark_mode() {
    return unsupported();
}
std::expected<void, std::string> set_dark_mode(bool /*dark*/) {
    return unsupported();
}
bool supports_lock() {
    return false;
}
std::expected<void, std::string> lock_screen() {
    return unsupported();
}
bool supports_notify() {
    return false;
}
std::expected<void, std::string> notify(const std::string& /*title*/, const std::string& /*body*/) {
    return unsupported();
}
bool supports_power() {
    return false;
}
std::expected<void, std::string> power(PowerAction /*action*/) {
    return unsupported();
}

std::string_view os_name() noexcept {
    return "système non pris en charge";
}

std::optional<std::string> distribution() {
    return std::nullopt;
}

std::optional<std::string> hostname() {
    return std::nullopt;
}

} // namespace avenoir::tools::platform
