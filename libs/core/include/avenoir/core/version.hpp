#pragma once

#include <string_view>

namespace avenoir::core {

/// Version du projet (`PROJECT_VERSION` de CMake), au format « majeur.mineur.correctif ».
[[nodiscard]] std::string_view version() noexcept;

} // namespace avenoir::core
