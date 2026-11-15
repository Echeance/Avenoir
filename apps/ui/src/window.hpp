#pragma once

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "avenoir/core/error.hpp"

// Fenêtre du widget : la vue web du système, en haut de l'écran. Le code
// propre à chaque système est dans platform/ (WebKitGTK 6 et layer-shell sous
// Linux ; WebView2 sous Windows, à venir).
namespace avenoir::ui {

struct WindowOptions {
    std::filesystem::path ui_directory; ///< Dossier de la page (index.html).
    /// Message JSON reçu de la page (fil de la fenêtre).
    std::function<void(std::string message)> from_page;
    bool developer_tools = false; ///< Inspecteur web (AVENOIR_UI_DEBUG).
    bool pinned = false;          ///< Widget toujours déplié (AVENOIR_UI_PIN).
    /// Fenêtre de l'application principale (`--app`) : ordinaire, avec
    /// bordure, redimensionnable, dans la barre des tâches.
    bool application = false;
    /// Vue de départ (AVENOIR_UI_VIEW) : de l'application (settings,
    /// journal…), ou, avec `pinned`, le module dont le widget montre l'onglet.
    std::string view;
};

class Window {
  public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;
    virtual ~Window() = default;

    /// Envoie un message JSON à la page ; depuis n'importe quel fil.
    virtual void post(std::string json) = 0;
    /// Hauteur de la fenêtre, en pixels (fil de la fenêtre).
    virtual void resize(int height) = 0;
    /// Boucle de la fenêtre, jusqu'à sa fermeture ou `quit`.
    virtual int run() = 0;
    /// Ferme la fenêtre ; depuis n'importe quel fil.
    virtual void quit() = 0;
};

/// Crée la fenêtre du widget (pas encore affichée). Erreur si ce système
/// n'est pas pris en charge ou si l'affichage manque.
[[nodiscard]] core::Result<std::unique_ptr<Window>> create_window(WindowOptions options);

/// Dossier de la page à côté de l'exécutable (`<dossier>/ui`), ou rien.
[[nodiscard]] std::optional<std::filesystem::path> default_ui_directory();

/// Nom de vue acceptable dans l'adresse : minuscules ASCII, 1 à 16.
[[nodiscard]] inline bool is_view_name(std::string_view view) {
    return !view.empty() && view.size() <= 16 &&
           std::ranges::all_of(view, [](char c) { return c >= 'a' && c <= 'z'; });
}

/// Lance `avenoir-ui --app [config]` (cet exécutable), détaché, sans shell.
void launch_application(const std::optional<std::filesystem::path>& config);

/// Largeur du widget, en pixels.
inline constexpr int kWidgetWidth = 560;
/// Taille de départ de l'application principale.
inline constexpr int kAppWidth = 1000;
inline constexpr int kAppHeight = 700;

} // namespace avenoir::ui
