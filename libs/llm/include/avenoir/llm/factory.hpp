#pragma once

#include <memory>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/llm.hpp"

namespace avenoir::llm {

/// Crée le fournisseur choisi dans la configuration. La clé d'API éventuelle
/// est lue dans la variable d'environnement dont la configuration donne le
/// *nom* ; elle n'apparaît jamais dans un message d'erreur ni dans un log.
[[nodiscard]] core::Result<std::unique_ptr<core::LlmProvider>>
make_provider(const core::LlmConfig& config);

} // namespace avenoir::llm
