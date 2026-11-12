#include "avenoir/agents/plan.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/core/error.hpp"

namespace avenoir::agents {

namespace {

using nlohmann::json;

constexpr std::size_t kMaxNameBytes = 40;
constexpr std::size_t kMaxInstructionBytes = 4000;
constexpr std::size_t kMaxRoleBytes = 200;
constexpr std::size_t kMaxStepsCeiling = 20;

std::unexpected<core::Error> plan_error(std::string message) {
    return core::make_error(core::ErrorCode::Config, "plan refusé : " + std::move(message));
}

std::optional<std::string> string_field(const json& object, std::string_view key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        return std::nullopt;
    }
    return found->get<std::string>();
}

/// Agent défini à la volée par le coordinateur.
core::Result<AgentSpec> parse_agent(const json& object, const std::vector<std::string>& allowed,
                                    const Limits& limits) {
    if (!object.is_object()) {
        return plan_error("chaque agent est un objet {name, role, tools}");
    }
    AgentSpec agent;
    agent.ephemeral = true;
    agent.name = string_field(object, "name").value_or("");
    agent.role = string_field(object, "role").value_or("");
    agent.instructions = string_field(object, "instructions").value_or("");
    if (!is_valid_name(agent.name)) {
        return plan_error(
            std::format("nom d'agent invalide « {} » (minuscules, chiffres, - et _)", agent.name));
    }
    if (agent.role.empty() || agent.role.size() > kMaxRoleBytes ||
        agent.instructions.size() > kMaxInstructionBytes) {
        return plan_error(
            std::format("agent « {} » : rôle manquant ou textes trop longs", agent.name));
    }
    if (const auto steps = object.find("max_steps");
        steps != object.end() && steps->is_number_unsigned()) {
        agent.max_steps = std::clamp<std::size_t>(steps->get<std::size_t>(), 1, kMaxStepsCeiling);
    }
    if (const auto tools = object.find("tools"); tools != object.end()) {
        if (!tools->is_array()) {
            return plan_error(std::format("agent « {} » : « tools » est une liste", agent.name));
        }
        for (const auto& tool : *tools) {
            if (!tool.is_string()) {
                return plan_error(std::format("agent « {} » : nom d'outil invalide", agent.name));
            }
            auto name = tool.get<std::string>();
            // Moindre privilège : jamais un outil que l'utilisateur n'a pas permis.
            if (!std::ranges::contains(allowed, name)) {
                return plan_error(std::format("agent « {} » : outil « {} » non permis aux "
                                              "agents",
                                              agent.name, name));
            }
            if (!std::ranges::contains(agent.tools, name)) {
                agent.tools.push_back(std::move(name));
            }
        }
    }
    if (agent.tools.size() > limits.max_tools_per_agent) {
        return plan_error(std::format("agent « {} » : trop d'outils ({} au plus)", agent.name,
                                      limits.max_tools_per_agent));
    }
    return agent;
}

/// Ordre topologique (Kahn) ; rien s'il y a un cycle.
std::optional<std::vector<Task>> topological(std::vector<Task> tasks) {
    std::map<std::string, std::size_t> remaining;
    std::map<std::string, std::vector<std::string>> dependents;
    for (const auto& task : tasks) {
        remaining[task.id] = task.depends_on.size();
        for (const auto& dependency : task.depends_on) {
            dependents[dependency].push_back(task.id);
        }
    }
    std::map<std::string, Task> by_id;
    std::vector<std::string> ready;
    for (auto& task : tasks) {
        if (task.depends_on.empty()) {
            ready.push_back(task.id);
        }
        by_id.emplace(task.id, std::move(task));
    }
    std::vector<Task> ordered;
    for (std::size_t i = 0; i < ready.size(); ++i) {
        const std::string id = ready[i];
        for (const auto& next : dependents[id]) {
            if (--remaining[next] == 0) {
                ready.push_back(next);
            }
        }
        ordered.push_back(std::move(by_id.at(id)));
    }
    if (ordered.size() != by_id.size()) {
        return std::nullopt;
    }
    return ordered;
}

} // namespace

bool is_valid_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= kMaxNameBytes && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

core::Result<Plan> parse_plan(const nlohmann::json& arguments,
                              const std::vector<AgentSpec>& configured,
                              const std::vector<std::string>& allowed_tools, Mode mode,
                              const Limits& limits) {
    if (!arguments.is_object() || !arguments.contains("tasks") || !arguments["tasks"].is_array() ||
        arguments["tasks"].empty()) {
        return plan_error("« tasks » doit être une liste d'au moins une tâche");
    }
    // Équipe disponible : agents configurés, puis ceux définis à la volée.
    std::map<std::string, AgentSpec> team;
    if (mode != Mode::Automatic) {
        for (const auto& agent : configured) {
            team.emplace(agent.name, agent);
        }
    }
    if (const auto defined = arguments.find("agents"); defined != arguments.end()) {
        if (!defined->is_array()) {
            return plan_error("« agents » doit être une liste");
        }
        if (mode == Mode::Configured && !defined->empty()) {
            return plan_error("seuls les agents configurés peuvent recevoir des tâches (mode « "
                              "configuré ») : " +
                              std::to_string(configured.size()) + " agent(s) disponible(s)");
        }
        if (defined->size() > limits.max_agents) {
            return plan_error(std::format("trop d'agents créés ({} au plus)", limits.max_agents));
        }
        for (const auto& object : *defined) {
            auto agent = parse_agent(object, allowed_tools, limits);
            if (!agent) {
                return std::unexpected(std::move(agent.error()));
            }
            if (team.contains(agent->name)) {
                return plan_error(std::format("agent « {} » déjà défini", agent->name));
            }
            team.emplace(agent->name, std::move(*agent));
        }
    }

    const auto& list = arguments["tasks"];
    if (list.size() > limits.max_tasks) {
        return plan_error(std::format("trop de tâches ({} au plus)", limits.max_tasks));
    }
    std::vector<Task> tasks;
    std::set<std::string> ids;
    for (const auto& object : list) {
        if (!object.is_object()) {
            return plan_error("chaque tâche est un objet {id, agent, instruction, depends_on}");
        }
        Task task;
        task.id = string_field(object, "id").value_or("");
        task.agent = string_field(object, "agent").value_or("");
        task.instruction = string_field(object, "instruction").value_or("");
        if (!is_valid_name(task.id)) {
            return plan_error(std::format("identifiant de tâche invalide « {} »", task.id));
        }
        if (!ids.insert(task.id).second) {
            return plan_error(std::format("tâche « {} » en double", task.id));
        }
        if (!team.contains(task.agent)) {
            return plan_error(
                std::format("tâche « {} » : agent « {} » inconnu", task.id, task.agent));
        }
        if (task.instruction.empty() || task.instruction.size() > kMaxInstructionBytes) {
            return plan_error(std::format("tâche « {} » : consigne vide ou trop longue", task.id));
        }
        if (const auto deps = object.find("depends_on"); deps != object.end()) {
            if (!deps->is_array()) {
                return plan_error(
                    std::format("tâche « {} » : « depends_on » est une liste", task.id));
            }
            for (const auto& dependency : *deps) {
                if (!dependency.is_string()) {
                    return plan_error(std::format("tâche « {} » : dépendance invalide", task.id));
                }
                auto name = dependency.get<std::string>();
                if (name == task.id) {
                    return plan_error(std::format("tâche « {} » : dépend d'elle-même", task.id));
                }
                if (!std::ranges::contains(task.depends_on, name)) {
                    task.depends_on.push_back(std::move(name));
                }
            }
        }
        tasks.push_back(std::move(task));
    }
    for (const auto& task : tasks) {
        for (const auto& dependency : task.depends_on) {
            if (!ids.contains(dependency)) {
                return plan_error(
                    std::format("tâche « {} » : dépend de « {} », inconnue", task.id, dependency));
            }
        }
    }
    auto ordered = topological(std::move(tasks));
    if (!ordered) {
        return plan_error("les dépendances forment un cycle");
    }
    Plan plan;
    std::set<std::string> used;
    for (const auto& task : *ordered) {
        used.insert(task.agent);
    }
    for (const auto& name : used) {
        plan.agents.push_back(team.at(name));
    }
    plan.tasks = std::move(*ordered);
    return plan;
}

} // namespace avenoir::agents
