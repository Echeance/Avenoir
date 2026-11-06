#pragma once

#include <string_view>

namespace avenoir::core {

/// Version du projet au format SemVer « majeur.mineur.correctif », suivie d'un
/// éventuel suffixe de pré-version (« 1.2.0 », « 1.2.0-beta.1 »).
[[nodiscard]] std::string_view version() noexcept;

} // namespace avenoir::core
