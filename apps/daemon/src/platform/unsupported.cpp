#include "avenoir/core/error.hpp"

#include "../autostart.hpp"

namespace avenoir::daemon {

core::Result<AutostartState> autostart_state() {
    return core::make_error(core::ErrorCode::Config, "démarrage automatique non pris en charge");
}

core::Result<AutostartState> set_autostart(bool /*enabled*/) {
    return core::make_error(core::ErrorCode::Config, "démarrage automatique non pris en charge");
}

} // namespace avenoir::daemon
