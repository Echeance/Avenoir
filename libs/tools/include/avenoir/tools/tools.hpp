#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "avenoir/core/error.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::tools {

/// Enregistre les outils natifs disponibles sur cette plateforme :
/// `current_time`, `system_info`, et si l'OS le permet `launch_app`, `open_url`.
[[nodiscard]] core::Result<void> register_native_tools(core::ToolRegistry& registry);

namespace detail {

/// Nom d'application accepté : lettres et chiffres ASCII, `.`, `-`, `_`, `+`,
/// espaces ; 100 caractères au plus ; ne commence ni par `-` ni par `.`.
/// Renvoie le nom débarrassé des espaces autour. Empêche toute option ou
/// tout chemin glissé dans la commande.
[[nodiscard]] std::expected<std::string, core::ToolError> validate_app_name(std::string_view name);

/// URL acceptée : `http://` ou `https://` suivi d'un hôte, sans espace ni
/// caractère de contrôle, 2048 caractères au plus.
[[nodiscard]] std::expected<std::string, core::ToolError> validate_url(std::string_view url);

/// « Il est 13 h 41, vendredi 6 novembre 2026. »
[[nodiscard]] std::string time_sentence(std::chrono::local_seconds time);

struct SystemFacts {
    std::string os;                          ///< « Linux », « Windows »…
    std::optional<std::string> distribution; ///< « CachyOS », « Windows 11 »…
    std::string arch;                        ///< « x86_64 », « arm64 »…
    std::optional<std::string> hostname;
    std::optional<std::size_t> cpus;
};

/// « Système : CachyOS (Linux, x86_64), machine « cachyos », 16 cœurs. »
[[nodiscard]] std::string system_sentence(const SystemFacts& facts);

} // namespace detail

} // namespace avenoir::tools
