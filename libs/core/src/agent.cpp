#include "avenoir/core/agent.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace avenoir::core {

namespace {

/// Journal d'audit : logger « audit » s'il a été créé par l'application,
/// sinon le logger par défaut.
std::shared_ptr<spdlog::logger> audit() {
    const auto logger = spdlog::get("audit");
    return logger != nullptr ? logger : spdlog::default_logger();
}

/// Coupe `text` après `max_chars` caractères Unicode, sans couper un
/// caractère UTF-8 en deux, et signale la troncature.
std::string truncate_utf8(std::string text, std::size_t max_chars) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        // Les octets de continuation UTF-8 sont de la forme 10xxxxxx.
        const auto byte = static_cast<unsigned char>(text[i]);
        if ((byte & 0xC0U) == 0x80U) {
            continue;
        }
        if (chars == max_chars) {
            text.resize(i);
            text += "\n[… résultat tronqué]";
            return text;
        }
        ++chars;
    }
    return text;
}

Error cancelled() {
    return Error{.code = ErrorCode::Cancelled, .message = {}};
}

} // namespace

Agent::Agent(std::unique_ptr<LlmProvider> llm, ToolRegistry tools,
             std::unique_ptr<ConfirmationHandler> confirmer, AgentOptions options)
    : llm_(std::move(llm)), tools_(std::move(tools)), confirmer_(std::move(confirmer)),
      options_(std::move(options)) {
    options_.max_steps = std::max<std::size_t>(options_.max_steps, 1);
}

std::string_view Agent::llm_name() const {
    return llm_ != nullptr ? llm_->name() : std::string_view{"aucun"};
}

Result<std::string> Agent::handle(std::string_view input, const std::stop_token& stop) {
    // 1. Voie rapide (sans LLM), si une règle correspond à un outil existant.
    if (options_.router) {
        if (auto route = options_.router->route(input); route && tools_.find(route->tool)) {
            spdlog::debug("voie rapide : {}", route->tool);
            const ToolCall call{.id = "fast-path", .name = route->tool, .arguments = std::move(route->arguments)};
            Execution result = execute(call);
            // Trace dans l'historique pour que le LLM ait le contexte ensuite.
            history_.emplace_back(UserMessage{.content = std::string(input)});
            history_.emplace_back(AssistantMessage{.text = result.content, .tool_calls = {}});
            return std::move(result.content);
        }
    }

    if (llm_ == nullptr || confirmer_ == nullptr) {
        return make_error(ErrorCode::Config, "agent sans fournisseur LLM ou sans confirmation");
    }

    // 2. Boucle agent.
    history_.emplace_back(UserMessage{.content = std::string(input)});
    const std::vector<ToolSpec> specs = tools_.specs();

    for (std::size_t step = 0; step < options_.max_steps; ++step) {
        if (stop.stop_requested()) {
            return std::unexpected(cancelled());
        }
        auto response = llm_->complete(LlmRequest{
            .system = options_.system_prompt,
            .messages = history_,
            .tools = specs,
            .stop = stop,
        });
        if (!response) {
            return std::unexpected(std::move(response.error()));
        }
        spdlog::debug("étape {} : {} appel(s) d'outil", step, response->tool_calls.size());

        if (response->tool_calls.empty()) {
            history_.emplace_back(AssistantMessage{.text = response->text, .tool_calls = {}});
            return std::move(response->text);
        }

        const std::vector<ToolCall> calls = response->tool_calls;
        history_.emplace_back(AssistantMessage{.text = std::move(response->text),
                                               .tool_calls = std::move(response->tool_calls)});
        for (const auto& call : calls) {
            if (stop.stop_requested()) {
                return std::unexpected(cancelled());
            }
            Execution result = execute(call);
            history_.emplace_back(ToolResultMessage{
                .call_id = call.id,
                .content = std::move(result.content),
                .is_error = result.is_error,
            });
        }
    }

    return make_error(ErrorCode::MaxStepsReached, std::to_string(options_.max_steps) + " étape(s)");
}

Agent::Execution Agent::execute(const ToolCall& call) {
    const auto tool = tools_.find(call.name);
    if (tool == nullptr) {
        spdlog::warn("outil inconnu demandé par le LLM : {}", call.name);
        return {.content = "Outil inconnu : " + call.name, .is_error = true};
    }
    const ToolSpec spec = tool->spec();

    switch (options_.policy.for_risk(spec.risk)) {
    case Policy::Allow:
        break;
    case Policy::Deny:
        audit()->info("refusé par la politique : {}", call.name);
        return {.content = "Action refusée par la politique de sécurité.", .is_error = true};
    case Policy::Confirm:
        if (confirmer_ == nullptr || confirmer_->confirm(spec, call) == Decision::Rejected) {
            audit()->info("refusé par l'utilisateur : {}", call.name);
            return {.content = "L'utilisateur a refusé cette action.", .is_error = true};
        }
        break;
    }

    audit()->info("exécution : {} {}", call.name, call.arguments.dump());
    auto output = tool->call(call.arguments);
    if (!output) {
        std::string message = describe(output.error());
        audit()->warn("échec : {} : {}", call.name, message);
        return {.content = std::move(message), .is_error = true};
    }
    return {.content = truncate_utf8(std::move(output->content), kMaxToolOutputChars),
            .is_error = false};
}

} // namespace avenoir::core
