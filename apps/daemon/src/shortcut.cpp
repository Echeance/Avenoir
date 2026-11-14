#include "shortcut.hpp"

#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace avenoir::daemon {

namespace {

/// Argument entre guillemets doubles (chemins avec espaces).
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + '"';
}

} // namespace

std::optional<std::string> quick_command(std::string_view terminal,
                                         const std::filesystem::path& cli) {
    const std::string program = quoted(cli.string()) + " --rapide";
    if (terminal == "foot") {
        return "foot --title=Avenoir --window-size-chars=100x24 " + program;
    }
    if (terminal == "kitty") {
        return "kitty --title Avenoir " + program;
    }
    if (terminal == "alacritty") {
        return "alacritty --title Avenoir -e " + program;
    }
    if (terminal == "wezterm") {
        return "wezterm start -- " + program;
    }
    if (terminal == "ghostty") {
        return "ghostty --title=Avenoir -e " + program;
    }
    if (terminal == "gnome-terminal") {
        return "gnome-terminal --title=Avenoir -- " + program;
    }
    if (terminal == "konsole") {
        return "konsole -e " + program;
    }
    if (terminal == "xterm") {
        return "xterm -T Avenoir -e " + program;
    }
    return std::nullopt;
}

std::optional<std::string> desktop_binding(std::string_view desktop, std::string_view command) {
    if (desktop == "niri") {
        // Chaîne KDL : guillemets et barres obliques inverses échappés.
        std::string escaped;
        for (const char c : command) {
            if (c == '"' || c == '\\') {
                escaped += '\\';
            }
            escaped += c;
        }
        return std::format("binds {{ Mod+Shift+A {{ spawn-sh \"{}\"; }} }}  "
                           "(~/.config/niri/config.kdl, dans le bloc binds existant)",
                           escaped);
    }
    if (desktop == "sway") {
        return std::format("bindsym $mod+Shift+a exec {}  (~/.config/sway/config)", command);
    }
    if (desktop == "Hyprland") {
        return std::format("bind = SUPER SHIFT, A, exec, {}  (~/.config/hypr/hyprland.conf)",
                           command);
    }
    return std::nullopt;
}

} // namespace avenoir::daemon
