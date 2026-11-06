#include "avenoir/core/version.hpp"

namespace avenoir::core {

std::string_view version() noexcept {
    return AVENOIR_VERSION;
}

} // namespace avenoir::core
