#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "avenoir/core/config.hpp"

namespace avenoir::cli {

/// Pose une question et rend la réponse ; rien si l'utilisateur abandonne
/// (Ctrl+D, Ctrl+C, fin de l'entrée).
using SetupAsk = std::function<std::optional<std::string>(const std::string& question)>;

struct SetupResult {
    core::AppConfig config;
    bool saved = false; ///< Écrite à `path` (sinon : pour cette session seulement).
};

/// Création guidée des modèles : recherche des modèles (cette machine,
/// réseau local sur demande, adresse saisie, Claude Code, clé Anthropic),
/// choix de l'utilisateur (ou « aucun » : voie rapide seule), puis
/// enregistrement à `path`. Les autres réglages sont ceux de `base`.
/// Rien si l'utilisateur abandonne.
[[nodiscard]] std::optional<SetupResult> run_setup(const std::filesystem::path& path,
                                                   const core::AppConfig& base, const SetupAsk& io);

/// Création guidée au premier lancement (aucun fichier à `path`, terminal
/// interactif), questions posées sur l'entrée standard. Renvoie la
/// configuration à utiliser ; rien si l'utilisateur abandonne.
[[nodiscard]] std::optional<core::AppConfig> run_first_setup(const std::filesystem::path& path);

} // namespace avenoir::cli
