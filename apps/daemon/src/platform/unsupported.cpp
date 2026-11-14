#include <cstdint>
#include <optional>

#include "avenoir/core/error.hpp"

#include "../autostart.hpp"
#include "../gpu.hpp"

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

} // namespace avenoir::daemon
