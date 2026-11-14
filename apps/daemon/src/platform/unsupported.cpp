#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "avenoir/core/error.hpp"

#include "../autostart.hpp"
#include "../gpu.hpp"
#include "../shortcut.hpp"

namespace avenoir::daemon {

core::Result<AutostartState> autostart_state() {
    return core::make_error(core::ErrorCode::Config, "démarrage automatique non pris en charge");
}

core::Result<AutostartState> set_autostart(bool /*enabled*/) {
    return core::make_error(core::ErrorCode::Config, "démarrage automatique non pris en charge");
}

std::optional<DemandingApp> demanding_app(std::uint64_t /*threshold_bytes*/) {
    return std::nullopt;
}

std::unique_ptr<GlobalShortcut>
register_quick_shortcut(const std::filesystem::path& /*cli*/,
                        const std::optional<std::filesystem::path>& /*config*/,
                        std::string& error) {
    error = "raccourci global non pris en charge sur ce système";
    return nullptr;
}

std::optional<std::string> native_shortcut() {
    return std::nullopt;
}

} // namespace avenoir::daemon
