#include <filesystem>
#include <memory>
#include <optional>

#include "avenoir/core/error.hpp"

#include "../window.hpp"

namespace avenoir::ui {

// Par valeur, comme la déclaration commune aux systèmes.
// NOLINTNEXTLINE(performance-unnecessary-value-param)
core::Result<std::unique_ptr<Window>> create_window(WindowOptions /*options*/) {
    return core::make_error(core::ErrorCode::Config,
                            "interface graphique pas encore prise en charge sur ce système");
}

void launch_application(const std::optional<std::filesystem::path>& /*config*/) {}

std::optional<std::filesystem::path> default_ui_directory() {
    return std::nullopt;
}

} // namespace avenoir::ui
