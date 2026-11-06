#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace avenoir::core {

/// Valeur d'une variable d'environnement, ou rien si elle est absente ou vide.
/// À appeler au démarrage, avant de lancer des threads (getenv n'est pas
/// garanti sûr en présence d'écritures concurrentes de l'environnement).
[[nodiscard]] std::optional<std::string> read_env(std::string_view name);

} // namespace avenoir::core
