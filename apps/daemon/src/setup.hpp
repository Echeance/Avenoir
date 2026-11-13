#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>

#include "avenoir/core/config.hpp"
#include "avenoir/core/error.hpp"

namespace avenoir::daemon {

/// Pose une question et rend la réponse ; rien si l'utilisateur abandonne
/// (Ctrl+D, Ctrl+C, fin de l'entrée).
using SetupAsk = std::function<std::optional<std::string>(const std::string& question)>;

/// Enregistre la configuration (réglages scellés, ou fichier simple en mode
/// développement).
using SaveSettings = std::function<core::Result<void>(const core::AppConfig& config)>;

struct SetupResult {
    core::AppConfig config;
    bool saved = false; ///< Écrite à `path` (sinon : pour cette session seulement).
};

/// Création guidée des modèles : recherche des modèles (cette machine,
/// réseau local sur demande, adresse saisie, Claude Code, clé Anthropic),
/// choix de l'utilisateur (ou « aucun » : voie rapide seule), puis
/// enregistrement par `writer` (`path` n'est que montré). Les autres réglages
/// sont ceux de `base`. Rien si l'utilisateur abandonne.
[[nodiscard]] std::optional<SetupResult> run_setup(const std::filesystem::path& path,
                                                   const core::AppConfig& base, const SetupAsk& io,
                                                   const SaveSettings& writer,
                                                   const std::stop_token& stop);

/// Étape de l'écran de verrouillage (Linux, session Wayland d'un compositeur
/// qui ne verrouille pas lui-même : niri, sway…) : installer swaylock par le
/// terminal du client (sudo, après accord), puis poser l'écran de
/// verrouillage d'Avenoir (configuration et fond dans ~/.config/swaylock), une
/// configuration existante n'étant remplacée que sur accord, et gardée.
/// `requested` : lancée par /verrouillage (dit pourquoi elle ne sert à rien
/// ici, au lieu de se taire).
void run_lock_setup(const SetupAsk& io, bool requested);

/// Création guidée au premier lancement (aucun fichier à `path`, terminal
/// interactif), questions posées au client qui se connecte. Renvoie la
/// configuration à utiliser ; rien si l'utilisateur abandonne.
[[nodiscard]] std::optional<core::AppConfig> run_first_setup(const std::filesystem::path& path,
                                                             const SaveSettings& writer,
                                                             const std::stop_token& stop);

} // namespace avenoir::daemon
