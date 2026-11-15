#pragma once

#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

// Vérification des mises à jour (décision 78) : seulement à la demande de
// l'utilisateur, une requête aux versions publiées du dépôt d'Avenoir sur
// GitHub, sans rien envoyer d'autre que la requête elle-même.
namespace avenoir::daemon {

/// Versions publiées d'Avenoir.
inline constexpr std::string_view kReleasesUrl =
    "https://api.github.com/repos/Echeance/Avenoir/releases?per_page=10";

struct Release {
    std::string version; ///< Sans « v » (« 0.3.0-beta.1 »).
    std::string url;     ///< Page de la version (https://github.com/…).
    bool prerelease = false;
};

/// Version publiée la plus récente dans la réponse de GitHub (liste de
/// versions) : les brouillons sont ignorés, les pré-versions aussi sauf si
/// `prereleases` ; rien si aucune n'est lisible.
[[nodiscard]] std::optional<Release> latest_release(const nlohmann::json& releases,
                                                    bool prereleases);

/// Vrai si `candidate` est plus récente que `current` (X.Y.Z, puis une
/// version sans suffixe passe devant `-alpha|beta|rc.N`).
[[nodiscard]] bool is_newer(std::string_view candidate, std::string_view current);

/// Demande la liste des versions à GitHub (5 s au plus, réponse bornée).
[[nodiscard]] core::Result<nlohmann::json> fetch_releases(const std::stop_token& stop);

} // namespace avenoir::daemon
