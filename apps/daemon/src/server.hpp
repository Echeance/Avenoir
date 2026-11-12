#pragma once

#include <filesystem>
#include <optional>
#include <stop_token>

#include "avenoir/ipc/transport.hpp"

// Le démon : vérifie les réglages, construit Avenoir, accepte les clients
// (CLI, plus tard l'application et la voix) et sert leurs requêtes une à une.
namespace avenoir::daemon {

struct DaemonOptions {
    /// Configuration donnée en argument (mode développement, non scellée) ;
    /// sinon celle de l'utilisateur, scellée.
    std::optional<std::filesystem::path> config;
    ipc::Endpoint endpoint;
    /// S'arrêter quand le dernier client part (démon privé d'une
    /// configuration de développement).
    bool exit_when_idle = false;
};

/// Sert jusqu'à `stop`, `/arreter`, ou le départ du dernier client
/// (`exit_when_idle`). Code de sortie du programme.
[[nodiscard]] int serve(const DaemonOptions& options, const std::stop_token& stop);

} // namespace avenoir::daemon
