#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>

// Surveillance des réglages pendant que le démon tourne (docs/PROJECT.md
// § 5.1) : l'état du fichier et de son sceau est relevé après chaque
// vérification et chaque écriture d'Avenoir ; s'il a changé depuis, quelqu'un
// d'autre y a touché, et le sceau est revérifié.
namespace avenoir::daemon {

/// Date de modification et taille d'un fichier ; rien s'il n'existe pas.
struct FileStamp {
    std::filesystem::file_time_type modified;
    std::uintmax_t size = 0;

    bool operator==(const FileStamp&) const = default;
};

[[nodiscard]] inline std::optional<FileStamp> stamp_of(const std::filesystem::path& path) {
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    return FileStamp{.modified = modified, .size = size};
}

/// État du fichier de réglages et de son sceau.
struct SettingsStamp {
    std::optional<FileStamp> config;
    std::optional<FileStamp> seal;

    bool operator==(const SettingsStamp&) const = default;
};

[[nodiscard]] inline SettingsStamp settings_stamp(const std::filesystem::path& config,
                                                  const std::filesystem::path& seal) {
    return SettingsStamp{.config = stamp_of(config), .seal = stamp_of(seal)};
}

} // namespace avenoir::daemon
