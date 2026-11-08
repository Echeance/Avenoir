#pragma once

// Interface du code propre à chaque OS (src/platform/<os>.cpp). Les arguments
// reçus ont déjà été validés par les outils (voir validation.cpp).

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app_index.hpp"

namespace avenoir::tools::platform {

/// Vrai si `launch_app` et `open_url` sont disponibles sur cet OS.
[[nodiscard]] bool supports_launching() noexcept;

/// Lance une application installée ou un exécutable du PATH (nom validé).
[[nodiscard]] std::expected<void, std::string> launch_app(const std::string& name);

/// Ouvre une URL validée dans le navigateur par défaut.
[[nodiscard]] std::expected<void, std::string> open_url(const std::string& url);

/// Applications installées : fichiers .desktop (Linux), dossier Applications
/// du menu Démarrer (Windows). Vide si l'OS n'en donne pas.
[[nodiscard]] std::vector<AppEntry> installed_apps();

/// Lance une application de l'index (jamais un nom venu tel quel du LLM).
[[nodiscard]] std::expected<void, std::string> launch_entry(const AppEntry& app);

/// Navigateur par défaut de l'utilisateur, s'il est connu.
[[nodiscard]] std::optional<AppEntry> default_browser();

// ─── Contrôle système (ROADMAP 2.1) ─────────────────────────────────────────
// Chaque fonction `supports_*` dit si l'outil correspondant peut être proposé
// sur cette machine (programme présent sous Linux, API disponible sous Windows).

/// Volume de la sortie audio par défaut, en pourcentage, et coupure du son.
struct VolumeState {
    int level = 0;
    bool muted = false;
};
[[nodiscard]] bool supports_volume();
[[nodiscard]] std::expected<VolumeState, std::string> get_volume();
[[nodiscard]] std::expected<void, std::string> set_volume(int level);
[[nodiscard]] std::expected<void, std::string> set_muted(bool muted);

/// Commandes multimédia envoyées au lecteur actif.
enum class MediaCommand : std::uint8_t { PlayPause, Play, Pause, Next, Previous, Stop };
[[nodiscard]] bool supports_media();
[[nodiscard]] std::expected<void, std::string> media_command(MediaCommand command);
/// « Titre — Artiste (en lecture) », si on peut le savoir.
[[nodiscard]] std::expected<std::string, std::string> now_playing();

/// Luminosité de l'écran intégré, en pourcentage.
[[nodiscard]] bool supports_brightness();
[[nodiscard]] std::expected<int, std::string> get_brightness();
[[nodiscard]] std::expected<void, std::string> set_brightness(int level);

/// Mode sombre du système.
[[nodiscard]] bool supports_dark_mode();
[[nodiscard]] std::expected<bool, std::string> get_dark_mode();
[[nodiscard]] std::expected<void, std::string> set_dark_mode(bool dark);

/// Verrouille la session.
[[nodiscard]] bool supports_lock();
[[nodiscard]] std::expected<void, std::string> lock_screen();

/// Notification du système (titre et texte déjà nettoyés).
[[nodiscard]] bool supports_notify();
[[nodiscard]] std::expected<void, std::string> notify(const std::string& title,
                                                      const std::string& body);

/// Alimentation : toujours sous confirmation (risque `sensitive`).
enum class PowerAction : std::uint8_t { Suspend, Shutdown, Reboot };
[[nodiscard]] bool supports_power();
[[nodiscard]] std::expected<void, std::string> power(PowerAction action);

[[nodiscard]] std::string_view os_name() noexcept;
[[nodiscard]] std::optional<std::string> distribution();
[[nodiscard]] std::optional<std::string> hostname();

} // namespace avenoir::tools::platform
