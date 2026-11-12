#pragma once

#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "avenoir/agents/plan.hpp"
#include "avenoir/core/agent.hpp"
#include "avenoir/core/error.hpp"
#include "avenoir/core/llm.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/tool.hpp"

// Exécution d'un plan (docs/PROJECT.md § 2.11) : chaque tâche est confiée à
// une instance de son agent (son modèle, sa consigne, ses seuls outils), les
// tâches indépendantes tournent en parallèle, une tâche attend le résultat de
// celles dont elle dépend ; un échec ou une annulation se propage à celles
// qui l'attendent.
namespace avenoir::agents {

/// Fournisseur LLM prêté à un agent pour une tâche.
struct ModelLease {
    std::unique_ptr<core::LlmProvider> llm;
    std::string label; ///< Nom du modèle (file d'attente des modèles locaux).
    bool local = true; ///< Un modèle local sert les agents à tour de rôle.
};

/// Ce qu'Avenoir fournit à l'orchestrateur.
struct Environment {
    /// Modèle d'un agent : celui qu'il demande, ou le modèle actif. Peut
    /// refuser (mode « local uniquement ») ou demander confirmation (modèle
    /// distant).
    std::function<core::Result<ModelLease>(const AgentSpec& agent)> model;
    /// Outils que les agents peuvent recevoir (jamais les outils agentiques).
    const core::ToolRegistry* tools = nullptr;
    /// Confirmations des actions : toujours l'utilisateur.
    std::function<std::unique_ptr<core::ConfirmationHandler>()> confirmer;
    core::PermissionPolicy policy;
    /// Demandes de l'utilisateur à l'origine du plan : seules tenues pour
    /// « demandées » par la provenance des arguments des agents.
    std::string requester_text;
    /// Début du prompt système de chaque agent (règles communes d'Avenoir).
    std::string base_prompt{core::kDefaultSystemPrompt};
    /// Exécute le travail d'une tâche dans le contexte de la requête (fil de
    /// travail : affichage, confirmations).
    std::function<void(const std::function<void()>& body)> context;
    /// Observateur de l'historique de chaque agent (actions montrées en direct).
    std::function<core::HistoryObserver(const AgentSpec& agent)> observer;
    std::function<void(const AgentSpec& agent, const Task& task)> on_start;
    /// Réponse du coordinateur à la question d'un agent (outil
    /// `ask_coordinator`) ; absente : l'outil n'est pas donné.
    std::function<core::Result<std::string>(std::string_view question, const std::stop_token& stop)>
        ask_coordinator;
};

/// Issue d'une tâche.
struct TaskOutcome {
    enum class Status : unsigned char {
        Done,      ///< Résultat rendu.
        Failed,    ///< Erreur de l'agent ou de son modèle.
        Skipped,   ///< Une tâche dont elle dépend a échoué ou été annulée.
        Cancelled, ///< Ctrl+C.
    };
    std::string id;
    std::string agent;
    Status status = Status::Failed;
    std::string output; ///< Résultat, ou raison de l'échec.
};

/// Exécute `plan` (validé par `parse_plan`) ; bloquant, interrompu par
/// `stop`. Les issues sont dans l'ordre du plan.
[[nodiscard]] std::vector<TaskOutcome> run_plan(const Plan& plan, const Environment& environment,
                                                const Limits& limits, const std::stop_token& stop);

/// Prompt système d'un agent : règles communes, puis son rôle et sa consigne.
[[nodiscard]] std::string agent_prompt(const AgentSpec& agent, std::string_view base);

/// Demande donnée à l'agent d'une tâche : sa consigne.
[[nodiscard]] std::string task_input(const Task& task);

/// Résultats des tâches dont `task` dépend, présentés comme des données
/// (vide s'il n'y en a pas). Ils sont donnés à l'agent comme un **résultat
/// d'outil**, jamais dans sa demande : ainsi, une adresse qu'un autre agent a
/// lue dans une page se fait confirmer (provenance des arguments) au lieu de
/// passer pour une demande de l'utilisateur.
[[nodiscard]] std::string dependency_results(const Task& task,
                                             const std::vector<TaskOutcome>& results);

/// Compte rendu pour le coordinateur : chaque tâche, son agent, son issue et
/// son résultat (borné), présentés comme des données.
[[nodiscard]] std::string summarize(const std::vector<TaskOutcome>& outcomes);

} // namespace avenoir::agents
