#pragma once

#include <string>
#include <vector>

#include "avenoir/core/error.hpp"

// Lancement du démon par la CLI quand il ne répond pas (décision 53).
namespace avenoir::cli {

/// Lance `avenoird` (à côté de cet exécutable) en arrière-plan, détaché de
/// ce terminal (nouvelle session, entrées et sorties fermées), avec
/// `arguments`. Ne l'attend pas.
[[nodiscard]] core::Result<void> start_daemon(const std::vector<std::string>& arguments);

} // namespace avenoir::cli
