// Code propre à Windows. Jamais de `cmd.exe` : `ShellExecuteW` résout les
// applications enregistrées (App Paths), le PATH et le navigateur par défaut.
// Chaînes en UTF-8 partout, converties en UTF-16 uniquement ici.

#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <shellapi.h>
#include <windows.h>

#include "avenoir/core/env.hpp"

#include "platform.hpp"

namespace avenoir::tools::platform {

namespace {

std::optional<std::wstring> to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring{};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (length <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                            static_cast<int>(utf8.size()), wide.data(), length) != length) {
        return std::nullopt;
    }
    return wide;
}

std::expected<void, std::string> shell_open(const std::string& target, const std::string& what) {
    const auto wide = to_wide(target);
    if (!wide) {
        return std::unexpected(what + " : texte UTF-8 invalide");
    }
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", wide->c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    // API historique : le code de retour est un entier rangé dans un HINSTANCE ;
    // une valeur supérieure à 32 signifie le succès. Seul cast de ce type, isolé ici.
    const auto code =
        reinterpret_cast<INT_PTR>(result); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (code <= 32) {
        return std::unexpected(what + " : échec (code " + std::to_string(code) + ")");
    }
    return {};
}

} // namespace

bool supports_launching() noexcept {
    return true;
}

std::expected<void, std::string> launch_app(const std::string& name) {
    return shell_open(name, "impossible de lancer « " + name + " »");
}

std::expected<void, std::string> open_url(const std::string& url) {
    return shell_open(url, "impossible d'ouvrir l'adresse");
}

std::string_view os_name() noexcept {
    return "Windows";
}

std::optional<std::string> distribution() {
    return std::nullopt;
}

std::optional<std::string> hostname() {
    return core::read_env("COMPUTERNAME");
}

} // namespace avenoir::tools::platform
