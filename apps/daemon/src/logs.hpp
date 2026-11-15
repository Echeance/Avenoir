#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace avenoir::daemon {

/// Fin du fichier texte `path` (un log) : `max` octets au plus, à partir d'un
/// début de ligne, octets UTF-8 invalides retirés (le texte part en JSON).
/// Vide si le fichier est absent ou illisible.
[[nodiscard]] std::string read_log_tail(const std::filesystem::path& path, std::size_t max);

} // namespace avenoir::daemon
