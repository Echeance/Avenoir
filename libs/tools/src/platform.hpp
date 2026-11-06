#pragma once

// Interface du code propre à chaque OS (src/platform/<os>.cpp). Les arguments
// reçus ont déjà été validés par les outils (voir validation.cpp).

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace avenoir::tools::platform {

/// Vrai si `launch_app` et `open_url` sont disponibles sur cet OS.
[[nodiscard]] bool supports_launching() noexcept;

/// Lance une application installée ou un exécutable du PATH (nom validé).
[[nodiscard]] std::expected<void, std::string> launch_app(const std::string& name);

/// Ouvre une URL validée dans le navigateur par défaut.
[[nodiscard]] std::expected<void, std::string> open_url(const std::string& url);

[[nodiscard]] std::string_view os_name() noexcept;
[[nodiscard]] std::optional<std::string> distribution();
[[nodiscard]] std::optional<std::string> hostname();

} // namespace avenoir::tools::platform
