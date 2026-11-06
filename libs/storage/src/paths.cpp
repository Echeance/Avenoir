#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "avenoir/core/env.hpp"
#include "avenoir/storage/store.hpp"

namespace avenoir::storage {

std::optional<std::filesystem::path> data_directory() {
    std::filesystem::path base;
#ifdef _WIN32
    const auto local = core::read_env("LOCALAPPDATA");
    if (!local) {
        return std::nullopt;
    }
    base = *local;
#else
    if (const auto xdg = core::read_env("XDG_DATA_HOME")) {
        base = *xdg;
    } else if (const auto home = core::read_env("HOME")) {
        base = std::filesystem::path(*home) / ".local" / "share";
    } else {
        return std::nullopt;
    }
#endif
    std::filesystem::path directory = base / "avenoir";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return std::nullopt;
    }
    // Conversations et saisies : lisibles par l'utilisateur seul (sous
    // Windows, %LOCALAPPDATA% est déjà propre à l'utilisateur).
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
    return directory;
}

std::string title_from(std::string_view text) {
    constexpr std::size_t kMaxBytes = 60;
    // Espaces et retours à la ligne ramenés à une seule espace.
    std::string compact;
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = !compact.empty();
            continue;
        }
        if (space) {
            compact += ' ';
            space = false;
        }
        compact += c;
    }
    if (compact.empty()) {
        return "Nouvelle conversation";
    }
    if (compact.size() <= kMaxBytes) {
        return compact;
    }
    // Coupe à la dernière espace avant la limite, sinon à la limite, sans
    // jamais couper un caractère UTF-8.
    std::size_t end = compact.rfind(' ', kMaxBytes);
    if (end == std::string::npos || end == 0) {
        end = kMaxBytes;
        while (end > 0 && (static_cast<unsigned char>(compact[end]) & 0xC0U) == 0x80U) {
            --end;
        }
    }
    return compact.substr(0, end) + "…";
}

} // namespace avenoir::storage
