#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/llm.hpp"

namespace avenoir::llm {

/// Clé d'API rangée par Avenoir (trousseau du système) sous le nom de la
/// variable que donne la configuration ; rien si elle n'y est pas.
using KeyLookup = std::function<std::optional<std::string>(std::string_view variable)>;

/// Crée le fournisseur choisi dans la configuration. La clé d'API éventuelle
/// est cherchée d'abord dans `keys` (trousseau, décision 63), puis dans la
/// variable d'environnement dont la configuration donne le *nom* ; elle
/// n'apparaît jamais dans un message d'erreur ni dans un log.
[[nodiscard]] core::Result<std::unique_ptr<core::LlmProvider>>
make_provider(const core::LlmConfig& config, const KeyLookup& keys = {});

} // namespace avenoir::llm
