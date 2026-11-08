#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "avenoir/core/error.hpp"
#include "avenoir/core/router.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::tools {

/// Enregistre les outils natifs disponibles sur cette plateforme :
/// `current_time`, `system_info`, et si l'OS le permet `launch_app`,
/// `list_apps`, `open_url` et le contrôle du système (`set_volume`,
/// `media_control`, `set_brightness`, `set_dark_mode`, `lock_screen`,
/// `notify`, `power`).
[[nodiscard]] core::Result<void> register_native_tools(core::ToolRegistry& registry);

/// Règles de voie rapide du contrôle du système (« baisse le son », « pause »,
/// « verrouille l'ordinateur »…). Une règle vers un outil absent sur cet OS
/// est sans effet : la requête part alors au LLM.
[[nodiscard]] core::Result<void> register_system_rules(core::FastRouter& router);

namespace detail {

/// Nom d'application accepté : lettres et chiffres ASCII, `.`, `-`, `_`, `+`,
/// espaces ; 100 caractères au plus ; ne commence ni par `-` ni par `.`.
/// Renvoie le nom débarrassé des espaces autour. Empêche toute option ou
/// tout chemin glissé dans la commande.
[[nodiscard]] std::expected<std::string, core::ToolError> validate_app_name(std::string_view name);

/// Nom d'application **à chercher** dans les applications installées : comme
/// `validate_app_name`, mais lettres accentuées et apostrophes permises
/// (« Bloc-notes », « Outil Capture d'écran ») ; jamais de caractère de
/// contrôle, de séparateur de commande ou de chemin, ni de marque
/// bidirectionnelle. Ce nom ne sert qu'à chercher ; seul un nom qui passe
/// aussi `validate_app_name` peut être lancé tel quel.
[[nodiscard]] std::expected<std::string, core::ToolError> validate_app_query(std::string_view name);

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
