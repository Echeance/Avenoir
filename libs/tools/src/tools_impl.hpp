#pragma once

// Fabriques internes des outils, utilisées par register.cpp.

#include <memory>

#include "avenoir/core/tool.hpp"

namespace avenoir::tools {

[[nodiscard]] std::shared_ptr<const core::Tool> make_current_time_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_system_info_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_launch_app_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_open_url_tool();

} // namespace avenoir::tools
