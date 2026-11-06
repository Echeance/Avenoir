// Repli pour les plateformes non prises en charge : les outils qui en
// dépendent ne sont pas enregistrés (supports_launching() renvoie faux).

#include <expected>
#include <optional>
#include <string>
#include <string_view>

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
