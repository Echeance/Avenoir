#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "avenoir/core/error.hpp"
#include "avenoir/core/tool.hpp"

// Commandes shell (outil `run_command`, décision 60) : désactivées par défaut
// (`[agent] shell`), analysées avant toute confirmation, toujours confirmées.
namespace avenoir::tools {

/// Ce que l'analyse d'une commande en retient.
struct ShellAssessment {
    /// Refusée d'office : élévation de droits, destruction du système,
    /// script téléchargé exécuté, shell inversé, persistance, réglages
    /// d'Avenoir. Raison lisible.
    std::optional<std::string> blocked;
    /// Alertes montrées dans le cadre de confirmation (suppression, secrets,
    /// envoi sur le réseau, commande masquée…).
    std::vector<std::string> warnings;
    /// Programmes lancés (premier mot de chaque commande simple), sans doublon.
    std::vector<std::string> programs;
};

/// Analyse statique d'une commande shell, sans rien exécuter. Une analyse ne
/// prouve jamais qu'une commande est sûre : elle refuse le pire et signale
/// le reste ; la confirmation de l'utilisateur reste la barrière.
[[nodiscard]] ShellAssessment assess_command(std::string_view command);

/// Avis à part sur une commande douteuse : le modèle actif, sans outils ni
/// historique. Ses mots ne font qu'ajouter une alerte, jamais lever un refus
/// ni une confirmation (il peut lui aussi être trompé).
using ShellReviewer =
    std::function<core::Result<std::string>(std::string_view command, const std::stop_token&)>;

/// Outil `run_command` : exécute une ligne dans le shell de l'utilisateur
/// (bash, sinon sh ; PowerShell sous Windows), dossier personnel, entrée
/// fermée, sortie bornée, Ctrl+C l'arrête. `reviewer` peut être vide.
[[nodiscard]] std::unique_ptr<core::Tool> make_shell_tool(ShellReviewer reviewer);

} // namespace avenoir::tools
