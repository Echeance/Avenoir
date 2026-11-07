#pragma once

// Code propre à chaque OS (src/platform/<os>.cpp), avec un repli « non supporté ».

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace avenoir::modules::platform {

/// POSIX : ignore SIGPIPE (une seule fois, s'il n'a pas de gestionnaire),
/// pour qu'écrire vers un module arrêté renvoie une erreur au lieu de tuer
/// Avenoir. Sans effet ailleurs.
void ignore_broken_pipe() noexcept;

/// Dossier de l'exécutable en cours.
[[nodiscard]] std::optional<std::filesystem::path> executable_directory();

/// Variables d'environnement transmises à tout module : le strict nécessaire
/// pour qu'un programme fonctionne (PATH, dossier personnel, langue…). Les
/// secrets d'Avenoir n'y sont pas.
[[nodiscard]] std::span<const std::string_view> base_environment();

/// Variables de proxy, transmises seulement aux modules qui ont la
/// permission « network ».
[[nodiscard]] std::span<const std::string_view> proxy_environment();

/// Fichiers candidats pour le programme `program` d'un module, dans son
/// dossier (sous Windows, avec l'extension .exe si elle manque).
[[nodiscard]] std::vector<std::filesystem::path>
program_candidates(const std::filesystem::path& directory, const std::filesystem::path& program);

} // namespace avenoir::modules::platform
