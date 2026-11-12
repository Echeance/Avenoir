#pragma once

#include <string>

#include "avenoir/core/error.hpp"

// Démarrage du démon à l'ouverture de session (/demarrage), sur demande de
// l'utilisateur seulement (décision 53) : unité systemd utilisateur (sinon
// entrée « autostart » XDG) sous Linux, entrée « Run » du compte sous Windows.
// Jamais de service système : le démon doit tourner dans la session de
// l'utilisateur, avec son bureau.
namespace avenoir::daemon {

struct AutostartState {
    bool enabled = false;
    std::string where; ///< Fichier ou clé concernés, pour l'utilisateur.
};

[[nodiscard]] core::Result<AutostartState> autostart_state();

/// Active ou retire le démarrage à l'ouverture de session pour ce démon
/// (chemin de l'exécutable en cours).
[[nodiscard]] core::Result<AutostartState> set_autostart(bool enabled);

} // namespace avenoir::daemon
