// Fenêtre du widget sous Linux : GTK 4, WebKitGTK 6 et, sous Wayland,
// gtk4-layer-shell (surface ancrée en haut de l'écran, au-dessus des
// fenêtres). Bibliothèques du système, liées dynamiquement (décision 68).
//
// La page est servie sous `avenoir://app/` depuis le dossier de l'interface,
// rien d'autre ne se charge : toute navigation ailleurs est refusée.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <spdlog/spdlog.h>
#include <webkit/webkit.h>

#include "avenoir/core/error.hpp"

#include "../assets.hpp"
#include "../relay.hpp"
#include "../window.hpp"

// Les macros de GLib (G_CALLBACK, GTK_WINDOW…) sont des casts C.
// Les fonctions à nombre variable d'arguments (g_object_new) sont l'API de GObject.
// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast,google-readability-casting,bugprone-casting-through-void,cppcoreguidelines-pro-type-vararg)

namespace avenoir::ui {

namespace {

constexpr std::string_view kOrigin = "avenoir://app/";

class GtkWidgetWindow final : public Window {
  public:
    explicit GtkWidgetWindow(WindowOptions options) : options_(std::move(options)) {}

    ~GtkWidgetWindow() override {
        if (loop_ != nullptr) {
            g_main_loop_unref(loop_);
        }
    }
    GtkWidgetWindow(const GtkWidgetWindow&) = delete;
    GtkWidgetWindow& operator=(const GtkWidgetWindow&) = delete;
    GtkWidgetWindow(GtkWidgetWindow&&) = delete;
    GtkWidgetWindow& operator=(GtkWidgetWindow&&) = delete;

    core::Result<void> build() {
        if (gtk_init_check() == FALSE) {
            return core::make_error(core::ErrorCode::Config,
                                    "aucun affichage : l'interface graphique demande une session "
                                    "graphique");
        }
        loop_ = g_main_loop_new(nullptr, FALSE);
        window_ = GTK_WINDOW(gtk_window_new());
        gtk_window_set_title(window_, "Avenoir");
        gtk_window_set_decorated(window_, FALSE);
        gtk_window_set_resizable(window_, FALSE);
        gtk_window_set_default_size(window_, kWidgetWidth, kMinHeight);
        gtk_widget_add_css_class(GTK_WIDGET(window_), "avenoir-widget");

        // Fond transparent : seul le widget dessiné par la page se voit.
        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_string(css,
                                          "window.avenoir-widget { background: transparent; }");
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                   GTK_STYLE_PROVIDER(css),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);

        // Wayland avec layer-shell (niri, sway, Hyprland, KDE) : en haut au
        // centre, au-dessus des fenêtres, sans réserver de place.
        if (gtk_layer_is_supported() != FALSE) {
            gtk_layer_init_for_window(window_);
            gtk_layer_set_namespace(window_, "avenoir");
            gtk_layer_set_layer(window_, GTK_LAYER_SHELL_LAYER_TOP);
            gtk_layer_set_anchor(window_, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
            gtk_layer_set_keyboard_mode(window_, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
        } else {
            spdlog::info("interface : layer-shell indisponible, fenêtre ordinaire");
        }

        // Fichiers de la page : schéma propre, servi par resolve_asset.
        WebKitWebContext* context = webkit_web_context_new();
        webkit_web_context_register_uri_scheme(context, "avenoir", &GtkWidgetWindow::on_asset, this,
                                               nullptr);
        webkit_security_manager_register_uri_scheme_as_secure(
            webkit_web_context_get_security_manager(context), "avenoir");

        WebKitUserContentManager* content = webkit_user_content_manager_new();
        webkit_user_content_manager_register_script_message_handler(content, "avenoir", nullptr);
        g_signal_connect(content, "script-message-received::avenoir",
                         G_CALLBACK(&GtkWidgetWindow::on_message), this);

        WebKitSettings* settings = webkit_settings_new();
        webkit_settings_set_enable_developer_extras(settings,
                                                    options_.developer_tools ? TRUE : FALSE);
        webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
        webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);
        webkit_settings_set_enable_html5_local_storage(settings, FALSE);
        webkit_settings_set_enable_html5_database(settings, FALSE);
        webkit_settings_set_enable_webrtc(settings, FALSE);
        webkit_settings_set_enable_media(settings, FALSE);

        view_ = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", context,
                                             "user-content-manager", content, "settings", settings,
                                             nullptr));
        g_object_unref(settings);
        g_object_unref(content);
        g_object_unref(context);
        const GdkRGBA transparent{.red = 0, .green = 0, .blue = 0, .alpha = 0};
        webkit_web_view_set_background_color(view_, &transparent);
        g_signal_connect(view_, "decide-policy", G_CALLBACK(&GtkWidgetWindow::on_policy), this);
        g_signal_connect(view_, "context-menu", G_CALLBACK(&GtkWidgetWindow::on_context_menu),
                         this);
        gtk_window_set_child(window_, GTK_WIDGET(view_));
        g_signal_connect(window_, "close-request", G_CALLBACK(&GtkWidgetWindow::on_close), this);
        return {};
    }

    void post(std::string json) override {
        // Exécuté dans le fil de la fenêtre. Un texte JSON est une expression
        // JavaScript valide : il est passé tel quel à l'entrée de la page.
        // Rendue à GLib, qui la détruit par la fonction donnée en dernier.
        auto task = std::make_unique<Task>(Task{.window = this, .text = std::move(json)});
        g_main_context_invoke_full(
            nullptr, G_PRIORITY_DEFAULT,
            [](gpointer data) -> gboolean {
                const auto* posted = static_cast<const Task*>(data);
                if (posted->window->view_ != nullptr) {
                    const std::string script =
                        "window.avenoirReceive && window.avenoirReceive(" + posted->text + ");";
                    webkit_web_view_evaluate_javascript(posted->window->view_, script.data(),
                                                        static_cast<gssize>(script.size()), nullptr,
                                                        nullptr, nullptr, nullptr, nullptr);
                }
                return G_SOURCE_REMOVE;
            },
            task.release(),
            [](gpointer data) { std::unique_ptr<Task>{static_cast<Task*>(data)}.reset(); });
    }

    void resize(int height) override {
        if (window_ != nullptr) {
            gtk_window_set_default_size(window_, kWidgetWidth, height);
        }
    }

    int run() override {
        const std::string start =
            std::string(kOrigin) + (options_.pinned ? "index.html#ouvert" : "index.html");
        webkit_web_view_load_uri(view_, start.c_str());
        gtk_window_present(window_);
        g_main_loop_run(loop_);
        view_ = nullptr;
        window_ = nullptr;
        return 0;
    }

    void quit() override {
        g_main_context_invoke(
            nullptr,
            [](gpointer data) -> gboolean {
                g_main_loop_quit(static_cast<GMainLoop*>(data));
                return G_SOURCE_REMOVE;
            },
            loop_);
    }

  private:
    struct Task {
        GtkWidgetWindow* window;
        std::string text;
    };

    /// Un fichier de la page, ou une erreur : rien hors du dossier de l'interface.
    static void on_asset(WebKitURISchemeRequest* request, gpointer data) {
        const auto* self = static_cast<const GtkWidgetWindow*>(data);
        const char* path = webkit_uri_scheme_request_get_path(request);
        const auto asset = resolve_asset(self->options_.ui_directory, path != nullptr ? path : "/");
        std::string content;
        if (asset) {
            std::ifstream file(asset->path, std::ios::binary);
            content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        if (!asset) {
            GError* error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "introuvable");
            webkit_uri_scheme_request_finish_error(request, error);
            g_error_free(error);
            return;
        }
        GBytes* bytes = g_bytes_new(content.data(), content.size());
        GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
        webkit_uri_scheme_request_finish(request, stream, static_cast<gint64>(content.size()),
                                         asset->mime.c_str());
        g_object_unref(stream);
        g_bytes_unref(bytes);
    }

    /// Message de la page (une chaîne JSON), donné au relais.
    static void on_message(WebKitUserContentManager* /*manager*/, JSCValue* value, gpointer data) {
        const auto* self = static_cast<const GtkWidgetWindow*>(data);
        if (jsc_value_is_string(value) == FALSE || !self->options_.from_page) {
            return;
        }
        char* text = jsc_value_to_string(value);
        std::string message(text);
        g_free(text);
        if (message.size() <= kMaxPageMessage) {
            self->options_.from_page(std::move(message));
        }
    }

    /// Navigation : seulement la page de l'interface ; pas de nouvelle fenêtre.
    static gboolean on_policy(WebKitWebView* /*view*/, WebKitPolicyDecision* decision,
                              WebKitPolicyDecisionType type, gpointer /*data*/) {
        if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
            return FALSE;
        }
        if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
            WebKitNavigationAction* action =
                webkit_navigation_policy_decision_get_navigation_action(
                    WEBKIT_NAVIGATION_POLICY_DECISION(decision));
            const char* uri =
                webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
            if (uri != nullptr && std::string_view(uri).starts_with(kOrigin)) {
                webkit_policy_decision_use(decision);
                return TRUE;
            }
        }
        spdlog::warn("interface : navigation refusée");
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }

    static gboolean on_context_menu(WebKitWebView* /*view*/, WebKitContextMenu* /*menu*/,
                                    WebKitHitTestResult* /*hit*/, gpointer data) {
        // Menu du navigateur (recharger, inspecter) : seulement en débogage.
        return static_cast<GtkWidgetWindow*>(data)->options_.developer_tools ? FALSE : TRUE;
    }

    static gboolean on_close(GtkWindow* /*window*/, gpointer data) {
        g_main_loop_quit(static_cast<GtkWidgetWindow*>(data)->loop_);
        return FALSE;
    }

    WindowOptions options_;
    GMainLoop* loop_ = nullptr;
    GtkWindow* window_ = nullptr;
    WebKitWebView* view_ = nullptr;
};

} // namespace

core::Result<std::unique_ptr<Window>> create_window(WindowOptions options) {
    auto window = std::make_unique<GtkWidgetWindow>(std::move(options));
    if (auto built = window->build(); !built) {
        return std::unexpected(std::move(built.error()));
    }
    return window;
}

std::optional<std::filesystem::path> default_ui_directory() {
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return std::nullopt;
    }
    return executable.parent_path() / "ui";
}

} // namespace avenoir::ui

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast,google-readability-casting,bugprone-casting-through-void,cppcoreguidelines-pro-type-vararg)
