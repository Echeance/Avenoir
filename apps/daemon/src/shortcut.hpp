#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

// Raccourci global de la saisie rapide (décision 67). Sous Windows, le démon
// enregistre Ctrl+Alt+Espace (RegisterHotKey) et ouvre à chaque appui une
// console avec « avenoir --rapide ». Sous Linux (Wayland), une application ne
// peut pas prendre un raccourci global : il se déclare dans le bureau
// (`shortcut_instructions`).
namespace avenoir::daemon {

/// Raccourci enregistré ; le retirer en le détruisant.
class GlobalShortcut {
  public:
    GlobalShortcut() = default;
    GlobalShortcut(const GlobalShortcut&) = delete;
    GlobalShortcut& operator=(const GlobalShortcut&) = delete;
    GlobalShortcut(GlobalShortcut&&) = delete;
    GlobalShortcut& operator=(GlobalShortcut&&) = delete;
    virtual ~GlobalShortcut() = default;
};

/// Enregistre le raccourci, qui lance `cli --rapide [config]` ; rien sur un
/// système où le bureau s'en charge, ou si la touche est déjà prise (le
/// message est alors dans `error`).
[[nodiscard]] std::unique_ptr<GlobalShortcut>
register_quick_shortcut(const std::filesystem::path& cli,
                        const std::optional<std::filesystem::path>& config, std::string& error);

/// Raccourci tel que le système l'affiche (« Ctrl+Alt+Espace »), ou rien
/// s'il se déclare dans le bureau.
[[nodiscard]] std::optional<std::string> native_shortcut();

/// Commande qui ouvre la saisie rapide dans un terminal, pour un raccourci
/// du bureau : `terminal` (« foot », « kitty »…) et ses arguments, puis
/// `cli --rapide`. Rien si le terminal est inconnu.
[[nodiscard]] std::optional<std::string> quick_command(std::string_view terminal,
                                                       const std::filesystem::path& cli);

/// Ligne à ajouter à la configuration du bureau `desktop` (niri, sway,
/// Hyprland…) pour lancer `command` par Super+Maj+A, ou rien s'il n'y a pas
/// de fichier à modifier (GNOME, KDE : réglages du bureau).
[[nodiscard]] std::optional<std::string> desktop_binding(std::string_view desktop,
                                                         std::string_view command);

} // namespace avenoir::daemon
