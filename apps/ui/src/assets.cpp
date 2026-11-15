#include "assets.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace avenoir::ui {

std::string_view mime_type(std::string_view extension) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kTypes{{
        {".html", "text/html"},
        {".js", "text/javascript"},
        {".css", "text/css"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".json", "application/json"},
        {".woff2", "font/woff2"},
    }};
    for (const auto& [known, mime] : kTypes) {
        if (extension == known) {
            return mime;
        }
    }
    return {};
}

std::optional<Asset> resolve_asset(const std::filesystem::path& root, std::string_view uri_path) {
    namespace fs = std::filesystem;
    if (uri_path.empty() || uri_path == "/") {
        uri_path = "/index.html";
    }
    // Lettres, chiffres et « . _ - / » seulement : ni « .. », ni « \ », ni
    // caractère encodé, ni contrôle.
    const bool plain = uri_path.size() <= 256 && uri_path.front() == '/' &&
                       std::ranges::all_of(uri_path, [](char c) {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
                                  c == '/';
                       });
    if (!plain || uri_path.contains("..") || uri_path.contains("//")) {
        return std::nullopt;
    }
    std::error_code error;
    const fs::path base = fs::canonical(root, error);
    if (error) {
        return std::nullopt;
    }
    const fs::path file = fs::canonical(base / uri_path.substr(1), error);
    if (error || !fs::is_regular_file(file, error)) {
        return std::nullopt;
    }
    // Un lien symbolique ne fait pas sortir du dossier de l'interface.
    const auto relative = file.lexically_relative(base);
    if (relative.empty() || relative.begin()->string() == "..") {
        return std::nullopt;
    }
    const std::string_view mime = mime_type(file.extension().string());
    if (mime.empty()) {
        return std::nullopt;
    }
    return Asset{.path = file, .mime = std::string(mime)};
}

namespace {

bool is_module_id(std::string_view id) {
    const auto allowed = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    return !id.empty() && id.size() <= 32 && allowed(id.front()) &&
           std::ranges::all_of(id, [&](char c) { return allowed(c) || c == '-'; });
}

} // namespace

std::string module_tab_policy(std::string_view origin) {
    const std::string own(origin);
    return "default-src 'none'; script-src " + own + "; style-src " + own +
           " 'unsafe-inline'; img-src " + own + " data:; font-src " + own +
           "; connect-src 'none'; frame-src 'none'; form-action 'none'; base-uri 'none'";
}

std::optional<ModuleAddress> parse_module_address(std::string_view uri) {
    constexpr std::string_view kLinux = "avenoir-module://";
    constexpr std::string_view kWindows = "https://";
    constexpr std::string_view kWindowsHost = ".module.avenoir";
    std::string_view module;
    std::string_view rest;
    std::string_view origin;
    if (uri.starts_with(kLinux)) {
        rest = uri.substr(kLinux.size());
        const auto slash = rest.find('/');
        module = rest.substr(0, slash);
        origin = uri.substr(0, kLinux.size() + module.size());
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
    } else if (uri.starts_with(kWindows)) {
        rest = uri.substr(kWindows.size());
        const auto slash = rest.find('/');
        const std::string_view host = rest.substr(0, slash);
        if (!host.ends_with(kWindowsHost)) {
            return std::nullopt;
        }
        module = host.substr(0, host.size() - kWindowsHost.size());
        origin = uri.substr(0, kWindows.size() + host.size());
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
    } else {
        return std::nullopt;
    }
    if (!is_module_id(module)) {
        return std::nullopt;
    }
    rest = rest.substr(0, rest.find_first_of("?#"));
    return ModuleAddress{.module = std::string(module),
                         .origin = std::string(origin),
                         .path = rest.empty() ? std::string("/") : std::string(rest)};
}

} // namespace avenoir::ui
