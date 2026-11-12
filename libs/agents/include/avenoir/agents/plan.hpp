#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

// Orchestrateur multi-agents (docs/PROJECT.md § 2.11) : le plan d'une demande
// découpée en tâches, chacune confiée à un agent, avec ses dépendances.
namespace avenoir::agents {

/// Agent qui peut recevoir une tâche : défini par l'utilisateur
/// (`[agents.<nom>]`) ou créé à la volée par le coordinateur pour ce plan.
struct AgentSpec {
    std::string name;                 ///< Minuscules, chiffres, « - » et « _ ».
    std::string role;                 ///< « développeur », « relecteur »…
    std::string instructions;         ///< Consigne propre à l'agent.
    std::optional<std::string> model; ///< `[models.<nom>]` ; absent : le modèle actif.
    std::vector<std::string> tools;   ///< Outils permis (noms exacts).
    std::size_t max_steps = 8;
    bool ephemeral = false; ///< Créé à la volée (supprimé à la fin du plan).

    bool operator==(const AgentSpec&) const = default;
};

/// Une tâche du plan.
struct Task {
    std::string id; ///< Unique dans le plan (minuscules, chiffres, « - », « _ »).
    std::string agent;
    std::string instruction;
    std::vector<std::string> depends_on; ///< Tâches dont le résultat est attendu.
};

/// Bornes d'un plan (réglables, `[orchestration]`).
struct Limits {
    std::size_t max_tasks = 12;
    std::size_t max_agents = 6; ///< Agents créés à la volée par plan.
    std::size_t max_parallel = 3;
    std::size_t max_tools_per_agent = 16;
};

/// Qui peut composer l'équipe d'un plan.
enum class Mode : unsigned char {
    Automatic,  ///< Agents créés à la volée seulement.
    Configured, ///< Agents de l'utilisateur seulement.
    Both,       ///< Les deux (par défaut).
};

/// Plan validé : tâches dans un ordre compatible avec leurs dépendances.
struct Plan {
    std::vector<AgentSpec> agents; ///< Ceux que le plan utilise.
    std::vector<Task> tasks;       ///< Ordre topologique.
};

/// Nom d'agent ou de tâche valide : `[a-z0-9_-]{1,40}`.
[[nodiscard]] bool is_valid_name(std::string_view name) noexcept;

/// Lit et valide le plan demandé par le coordinateur (arguments de
/// `delegate_tasks`) : agents connus (`configured`) ou définis à la volée
/// (selon `mode`), outils de chaque agent compris dans `allowed_tools` (un
/// agent ne reçoit jamais plus que ce qui est permis), identifiants uniques,
/// dépendances existantes, aucun cycle, bornes respectées. Les erreurs sont
/// faites pour être lues par le coordinateur, qui peut corriger son plan.
[[nodiscard]] core::Result<Plan> parse_plan(const nlohmann::json& arguments,
                                            const std::vector<AgentSpec>& configured,
                                            const std::vector<std::string>& allowed_tools,
                                            Mode mode, const Limits& limits);

} // namespace avenoir::agents
