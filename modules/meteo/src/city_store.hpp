#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "weather.hpp"

namespace avenoir::meteo {

/// Ville par défaut de l'utilisateur, gardée dans le dossier de données du
/// module (`AVENOIR_MODULE_DATA`, fichier `ville.json`).
class CityStore {
  public:
    /// Sans dossier, rien ne peut être retenu.
    explicit CityStore(std::optional<std::filesystem::path> directory);

    [[nodiscard]] std::optional<Place> load() const;
    /// Enregistre la ville (ou l'efface si `place` est vide), de façon
    /// atomique (fichier temporaire puis renommage).
    [[nodiscard]] Result<void> save(const std::optional<Place>& place) const;

    /// Change la ville par défaut. Renvoie le jeton d'annulation, qui retient
    /// l'ancienne ville et la nouvelle.
    [[nodiscard]] Result<std::string> change(const Place& place) const;

    /// Défait un changement d'après son jeton, seulement si la ville par
    /// défaut est toujours celle qu'il a posée. Renvoie la phrase à afficher.
    [[nodiscard]] Result<std::string> undo(std::string_view token) const;

  private:
    std::optional<std::filesystem::path> file_;
};

} // namespace avenoir::meteo
