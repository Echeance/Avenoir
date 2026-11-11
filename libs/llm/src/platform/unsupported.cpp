#include <optional>
#include <string>

#include "../platform.hpp"

namespace avenoir::llm::platform {

std::optional<std::string> default_route_ipv4() {
    return std::nullopt;
}

} // namespace avenoir::llm::platform
