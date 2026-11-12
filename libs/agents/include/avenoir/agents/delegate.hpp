#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "avenoir/agents/orchestrator.hpp"
#include "avenoir/agents/plan.hpp"
#include "avenoir/core/tool.hpp"

// Outil agentique du coordinateur (docs/PROJECT.md § 2.11) : `delegate_tasks`
// découpe une demande en tâches confiées à des agents. Réservé au
// coordinateur : les agents ne le reçoivent jamais (une seule profondeur de
// délégation).
namespace avenoir::agents {

/// Nom de l'outil de délégation.
inline constexpr std::string_view kDelegateTool = "delegate_tasks";

struct DelegateOptions {
    /// Agents de l'utilisateur (`[agents.<nom>]`).
    std::vector<AgentSpec> configured;
    /// Outils que les agents peuvent recevoir (jamais `delegate_tasks`).
    std::function<std::vector<std::string>()> allowed_tools;
    Mode mode = Mode::Both;
    Limits limits;
    /// Environnement d'exécution, créé à chaque appel (dans le contexte de la
    /// requête en cours : affichage, confirmations).
    std::function<Environment()> environment;
    /// Rendu des issues aux yeux de l'utilisateur, une fois le plan fini.
    std::function<void(const std::vector<TaskOutcome>& outcomes)> on_finished;
};

[[nodiscard]] std::shared_ptr<core::Tool> make_delegate_tool(DelegateOptions options);

} // namespace avenoir::agents
