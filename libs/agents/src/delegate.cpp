#include "avenoir/agents/delegate.hpp"

#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/agents/orchestrator.hpp"
#include "avenoir/agents/plan.hpp"
#include "avenoir/core/tool.hpp"

namespace avenoir::agents {

namespace {

using nlohmann::json;

std::string list_names(const std::vector<std::string>& names) {
    std::string text;
    for (const auto& name : names) {
        text += (text.empty() ? "" : ", ") + name;
    }
    return text.empty() ? "aucun" : text;
}

class DelegateTool final : public core::Tool {
  public:
    explicit DelegateTool(DelegateOptions options) : options_(std::move(options)) {}

    [[nodiscard]] core::ToolSpec spec() const override {
        const auto allowed =
            options_.allowed_tools ? options_.allowed_tools() : std::vector<std::string>{};
        std::string team;
        if (options_.mode != Mode::Automatic) {
            for (const auto& agent : options_.configured) {
                team += std::format("\n- « {} » : {} (outils : {})", agent.name, agent.role,
                                    list_names(agent.tools));
            }
        }
        std::string description =
            "Confie une demande longue ou en plusieurs parties à une équipe d'agents qui "
            "travaillent en parallèle, puis rend le compte rendu de chaque tâche (tu rédiges "
            "ensuite la réponse à l'utilisateur). À utiliser seulement quand la demande gagne à "
            "être découpée (plusieurs recherches, comparer des éléments, préparer puis relire) ; "
            "une demande simple se traite directement. Chaque tâche : un identifiant, l'agent qui "
            "la fait, une consigne complète et autonome, et les tâches dont elle attend le "
            "résultat (depends_on).";
        if (!team.empty()) {
            description += "\nAgents de l'utilisateur :" + team;
        }
        if (options_.mode != Mode::Configured) {
            description += "\nTu peux aussi créer des agents pour ce plan (« agents » : name, "
                           "role, tools), avec seulement les outils nécessaires parmi : " +
                           list_names(allowed) + ".";
        } else if (team.empty()) {
            description += "\nAucun agent n'est configuré : n'utilise pas cet outil.";
        }
        description += std::format("\nAu plus {} tâches et {} agents créés.",
                                   options_.limits.max_tasks, options_.limits.max_agents);
        json task = {
            {"type", "object"},
            {"properties",
             {{"id",
               {{"type", "string"},
                {"description", "identifiant court : minuscules sans accent, chiffres, - et _"}}},
              {"agent", {{"type", "string"}, {"description", "nom de l'agent"}}},
              {"instruction", {{"type", "string"}, {"description", "consigne complète"}}},
              {"depends_on",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "tâches dont le résultat est attendu"}}}}},
            {"required", {"id", "agent", "instruction"}}};
        json agent = {
            {"type", "object"},
            {"properties",
             {{"name",
               {{"type", "string"},
                {"description", "minuscules sans accent, chiffres, - et _ (ex. meteo_lyon)"}}},
              {"role", {{"type", "string"}}},
              {"instructions", {{"type", "string"}}},
              {"tools", {{"type", "array"}, {"items", {{"type", "string"}}}}}}},
            {"required", {"name", "role", "tools"}}};
        json properties = {{"tasks", {{"type", "array"}, {"items", task}}}};
        if (options_.mode != Mode::Configured) {
            properties["agents"] = {{"type", "array"}, {"items", agent}};
        }
        return core::ToolSpec{
            .name = std::string(kDelegateTool),
            .description = std::move(description),
            .input_schema = {{"type", "object"},
                             {"properties", std::move(properties)},
                             {"required", {"tasks"}}},
            // Déléguer n'agit pas en soi : chaque action des agents passe par
            // les permissions (et les confirmations) comme les autres.
            .risk = core::RiskLevel::Reversible,
        };
    }

    [[nodiscard]] core::ToolResult call(const nlohmann::json& arguments,
                                        const std::stop_token& stop) const override {
        const auto allowed =
            options_.allowed_tools ? options_.allowed_tools() : std::vector<std::string>{};
        auto plan =
            parse_plan(arguments, options_.configured, allowed, options_.mode, options_.limits);
        if (!plan) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::InvalidArguments,
                                                   .message = plan.error().message});
        }
        if (!options_.environment) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Unsupported,
                                                   .message = "orchestration indisponible"});
        }
        const auto outcomes = run_plan(*plan, options_.environment(), options_.limits, stop);
        if (options_.on_finished) {
            options_.on_finished(outcomes);
        }
        if (stop.stop_requested()) {
            return std::unexpected(core::ToolError{.kind = core::ToolErrorKind::Execution,
                                                   .message = "plan interrompu (Ctrl+C)"});
        }
        return core::ToolOutput{.content = summarize(outcomes)};
    }

  private:
    DelegateOptions options_;
};

} // namespace

std::shared_ptr<core::Tool> make_delegate_tool(DelegateOptions options) {
    return std::make_shared<DelegateTool>(std::move(options));
}

} // namespace avenoir::agents
