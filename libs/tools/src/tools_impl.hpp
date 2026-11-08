#pragma once

// Fabriques internes des outils, utilisées par register.cpp.

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/tool.hpp"

namespace avenoir::tools {

[[nodiscard]] std::shared_ptr<const core::Tool> make_current_time_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_system_info_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_launch_app_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_list_apps_tool();
[[nodiscard]] std::shared_ptr<const core::Tool> make_open_url_tool();
/// Outils de contrôle du système disponibles sur cet OS.
[[nodiscard]] std::vector<std::shared_ptr<const core::Tool>> make_system_control_tools();

namespace detail {

/// Entier facultatif `key` de `arguments` : nombre JSON ou texte (« 30 »,
/// « 30 % », « -10 ») venu de la voie rapide. Vide s'il est absent ; erreur
/// s'il est illisible ou hors de [min, max].
[[nodiscard]] std::expected<std::optional<int>, core::ToolError>
optional_int(const nlohmann::json& arguments, const char* key, int min, int max);

/// Texte d'une notification : caractères de contrôle remplacés par une
/// espace, marques bidirectionnelles et invisibles retirées, coupé à
/// `max_bytes` sans couper un caractère UTF-8.
[[nodiscard]] std::string clean_notification(std::string_view text, std::size_t max_bytes);

} // namespace detail

} // namespace avenoir::tools
