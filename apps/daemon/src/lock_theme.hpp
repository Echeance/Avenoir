#pragma once

#include <optional>
#include <string>
#include <string_view>

// Écran de verrouillage d'Avenoir pour swaylock (création guidée,
// /verrouillage) : ce qui se décide sans écran ni système, testé à part.
namespace avenoir::daemon {

/// Bureau qui verrouille lui-même la session quand logind le demande
/// (GNOME, KDE Plasma, Cinnamon, Xfce…) : swaylock y est inutile.
/// `desktop` : valeur de XDG_CURRENT_DESKTOP (« niri », « KDE », « ubuntu:GNOME »).
[[nodiscard]] bool desktop_locks_itself(std::string_view desktop);

/// L'étape du verrouillage a un sens : session Wayland d'un compositeur qui
/// ne verrouille pas lui-même (niri, sway, river, labwc, Hyprland…).
[[nodiscard]] bool lock_step_relevant(std::string_view session_type, std::string_view desktop);

/// Première ligne des fichiers écrits par Avenoir : les reconnaître ensuite.
inline constexpr std::string_view kLockConfigMarker = "# Avenoir — écran de verrouillage";

/// Configuration swaylock d'Avenoir : fond bleu nuit, le loup et le titre
/// (`image_path`), anneau bleu, frappe en bleu glacier, erreur en rose.
[[nodiscard]] std::string swaylock_config(std::string_view image_path);

/// Ce qu'il y a déjà à l'emplacement de la configuration swaylock.
enum class LockConfigState : unsigned char {
    Missing, ///< Aucun fichier.
    Avenoir, ///< Écrit par Avenoir (première ligne `kLockConfigMarker`).
    Other,   ///< Écrit par l'utilisateur ou un autre programme : à garder.
};
[[nodiscard]] LockConfigState lock_config_state(const std::optional<std::string>& content);

/// Fond de l'écran de verrouillage, en PNG : sur une image bleu nuit centrée
/// sur l'écran, le loup à l'épée au-dessus de l'anneau de swaylock et le
/// titre « AVENOIR » en blocs dessous (couleurs de l'en-tête de la CLI).
[[nodiscard]] std::string lock_image_png();

/// Taille de `lock_image_png` (et place laissée à l'anneau au centre).
inline constexpr int kLockImageWidth = 1000;
inline constexpr int kLockImageHeight = 960;

} // namespace avenoir::daemon
