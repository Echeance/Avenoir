#include <optional>
#include <string>
#include <string_view>
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

std::optional<std::string> read_hidden_line() {
    return std::nullopt;
}

bool copy_to_clipboard(std::string_view /*text*/) {
    return false;
}

} // namespace avenoir::cli
