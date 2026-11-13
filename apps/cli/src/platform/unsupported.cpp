#include <optional>
#include <string>
#include <vector>

#include "avenoir/core/error.hpp"

#include "../launcher.hpp"

namespace avenoir::cli {

core::Result<void> start_daemon(const std::vector<std::string>& /*arguments*/) {
    return core::make_error(core::ErrorCode::Ipc, "démon non pris en charge sur ce système");
}

std::optional<int> run_in_terminal(const std::vector<std::string>& /*arguments*/) {
    return std::nullopt; // seulement sous Linux (installation de swaylock)
}

} // namespace avenoir::cli
