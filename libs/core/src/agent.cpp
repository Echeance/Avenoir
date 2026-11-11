#include "avenoir/core/agent.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
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

void Agent::set_llm(std::unique_ptr<LlmProvider> llm) {
    if (llm != nullptr) {
        llm_ = std::move(llm);
    }
}

void Agent::begin_journal(std::string_view input) {
    // Nouveau journal d'annulation pour cette requête. Celui de la requête
    // précédente est oublié s'il est vide ; on n'en garde qu'un nombre limité.
    if (!journals_.empty() && journals_.back().actions.empty()) {
        journals_.pop_back();
    }
    journals_.push_back(RequestJournal{.request = std::string(input), .actions = {}});
    while (journals_.size() > kMaxUndoJournals) {
        journals_.pop_front();
    }
}

Result<std::string> Agent::run_direct(std::string_view input, const ToolCall& call,
                                      const std::stop_token& stop) {
    Execution result = execute(call, stop);
    // Trace dans l'historique pour que le LLM ait le contexte ensuite.
    append(UserMessage{.content = std::string(input)});
    // Interrompue (Ctrl+C) : l'échec rendu par l'outil n'est que la conséquence
    // de l'arrêt ; l'action a pu avoir lieu en partie (journal conservé).
    if (stop.stop_requested()) {
        append(AssistantMessage{.text = "Action interrompue par l'utilisateur.", .tool_calls = {}});
        return std::unexpected(cancelled());
    }
    append(AssistantMessage{.text = result.content, .tool_calls = {}});
    return std::move(result.content);
}

Result<std::string> Agent::run_tool(std::string_view input, ToolCall call,
                                    const std::stop_token& stop) {
    if (tools_.find(call.name) == nullptr) {
        return make_error(ErrorCode::Config, "outil inconnu : " + call.name);
    }
    begin_journal(input);
    if (call.id.empty()) {
        call.id = "command";
    }
    return run_direct(input, call, stop);
}

Result<std::string> Agent::handle(std::string_view input, const std::stop_token& stop) {
    begin_journal(input);

    // 1. Voie rapide (sans LLM), si une règle correspond à un outil existant.
    if (options_.router) {
        if (auto route = options_.router->route(input); route && tools_.find(route->tool)) {
            spdlog::debug("voie rapide : {}", route->tool);
            const ToolCall call{
                .id = "fast-path", .name = route->tool, .arguments = std::move(route->arguments)};
            return run_direct(input, call, stop);
        }
    }

    if (llm_ == nullptr) {
        return make_error(ErrorCode::Config,
                          "aucun modèle : seule la voie rapide répond (ajoutez une section "
                          "[models.<nom>] à config.toml)");
    }
    if (confirmer_ == nullptr) {
        return make_error(ErrorCode::Config, "agent sans confirmation");
    }

    // 2. Boucle agent.
    append(UserMessage{.content = std::string(input)});
    const std::vector<ToolSpec> specs = tools_.specs();
    std::string system =
        model_identity_.empty()
            ? options_.system_prompt
            : options_.system_prompt +
                  "\n\nModèle actif (celui qui te fait fonctionner) : " + model_identity_ + ".";
    std::vector<ToolCall> request_calls; // appels de cette requête, pour les vérifications
    bool reminded = false;

    for (std::size_t step = 0; step < options_.max_steps; ++step) {
        if (stop.stop_requested()) {
            return std::unexpected(cancelled());
        }
        auto response = llm_->complete(LlmRequest{
            .system = system,
            .messages = history_,
            .tools = specs,
            .stop = stop,
        });
        if (!response) {
            return std::unexpected(std::move(response.error()));
        }
        spdlog::debug("étape {} : {} appel(s) d'outil", step, response->tool_calls.size());

        if (response->tool_calls.empty()) {
            // Ni texte ni outil : le modèle n'a rien répondu. On le signale
            // plutôt que d'afficher une réponse vide. Si rien d'autre ne
            // s'est passé, la question est retirée de l'historique pour
            // pouvoir être reposée telle quelle ; sinon (des outils ont déjà
            // agi), l'historique est conservé.
            if (response->text.empty()) {
                if (step == 0) {
                    remove_last();
                }
                return make_error(ErrorCode::Llm, "le modèle n'a renvoyé aucune réponse");
            }
            if (auto issue = check_reply(response->text, request_calls)) {
                if (!reminded) {
                    // Réponse écartée : le modèle est relancé avec le rappel.
                    spdlog::warn("réponse annonçant une action non faite, relance : {}",
                                 response->text);
                    reminded = true;
                    system += "\n\n" + issue->reminder;
                    continue;
                }
                response->text += "\n\n" + issue->warning;
            }
            append(AssistantMessage{
                .text = response->text, .tool_calls = {}, .usage = response->usage});
            return std::move(response->text);
        }

        const std::vector<ToolCall> calls = response->tool_calls;
        request_calls.insert(request_calls.end(), calls.begin(), calls.end());
        append(AssistantMessage{.text = std::move(response->text),
                                .tool_calls = std::move(response->tool_calls),
                                .usage = response->usage});
        for (const auto& call : calls) {
            if (stop.stop_requested()) {
                return std::unexpected(cancelled());
            }
            Execution result = execute(call, stop);
            append(ToolResultMessage{
                .call_id = call.id,
                .content = std::move(result.content),
                .is_error = result.is_error,
            });
        }
    }

    return make_error(ErrorCode::MaxStepsReached, std::to_string(options_.max_steps) + " étape(s)");
}

Result<std::string> Agent::ask_aside(std::string_view system, std::string text,
                                     const std::stop_token& stop) {
    if (llm_ == nullptr) {
        return make_error(ErrorCode::Config, "aucun modèle configuré");
    }
    const std::vector<Message> messages{UserMessage{.content = std::move(text)}};
    auto response = llm_->complete(LlmRequest{
        .system = system,
        .messages = messages,
        .tools = {},
        .stop = stop,
    });
    if (!response) {
        return std::unexpected(std::move(response.error()));
    }
    if (response->text.empty()) {
        return make_error(ErrorCode::Llm, "le modèle n'a renvoyé aucune réponse");
    }
    return std::move(response->text);
}

std::optional<ReplyIssue> Agent::check_reply(std::string_view reply,
                                             std::span<const ToolCall> calls) const {
    for (const auto& check : reply_checks_) {
        if (auto issue = check(reply, calls)) {
            return issue;
        }
    }
    return std::nullopt;
}

Agent::Execution Agent::execute(const ToolCall& call, const std::stop_token& stop) {
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
    auto output = tool->call(call.arguments, stop);
    if (!output) {
        std::string message = describe(output.error());
        audit()->warn("échec : {} : {}", call.name, message);
        return {.content = std::move(message), .is_error = true};
    }
    // Journal d'annulation : seules les actions à effet de bord y figurent.
    if (spec.risk != RiskLevel::ReadOnly && !journals_.empty()) {
        journals_.back().actions.push_back(ActionRecord{
            .tool = call.name,
            .summary = call.name + " " + call.arguments.dump(),
            .undo = std::move(output->undo),
        });
    }
    return {.content = truncate_utf8(std::move(output->content), kMaxToolOutputChars),
            .is_error = false};
}

void Agent::append(Message message) {
    history_.push_back(std::move(message));
    if (observer_.appended) {
        observer_.appended(history_.back());
    }
}

void Agent::remove_last() {
    if (history_.empty()) {
        return;
    }
    history_.pop_back();
    if (observer_.removed_last) {
        observer_.removed_last();
    }
}

std::size_t RequestJournal::undoable_count() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(actions, [](const ActionRecord& a) { return a.undo.has_value(); }));
}

const RequestJournal* Agent::last_undoable() const {
    for (const auto& journal : journals_ | std::views::reverse) {
        if (journal.undoable_count() > 0) {
            return &journal;
        }
    }
    return nullptr;
}

std::vector<UndoOutcome> Agent::undo_last() {
    std::vector<UndoOutcome> outcomes;
    const auto it =
        std::ranges::find_if(journals_ | std::views::reverse, [](const RequestJournal& journal) {
            return journal.undoable_count() > 0;
        });
    if (it == journals_.rend()) {
        return outcomes;
    }
    // Ordre inverse : la dernière action est défaite en premier.
    for (const auto& action : it->actions | std::views::reverse) {
        if (!action.undo || !action.undo->run) {
            continue;
        }
        const std::string& description = action.undo->description;
        audit()->info("annulation : {} ({})", description, action.tool);
        auto result = action.undo->run();
        if (result) {
            outcomes.push_back(
                {.description = description, .succeeded = true, .message = result->content});
        } else {
            std::string message = describe(result.error());
            audit()->warn("échec de l'annulation : {} : {}", description, message);
            outcomes.push_back(
                {.description = description, .succeeded = false, .message = std::move(message)});
        }
    }
    journals_.erase(std::next(it).base());
    return outcomes;
}

} // namespace avenoir::core
