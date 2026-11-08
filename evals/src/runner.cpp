#include "runner.hpp"

#include <chrono>
#include <utility>
#include <variant>

#include "avenoir/core/agent.hpp"

namespace avenoir::evals {

namespace {

/// Répond comme prévu par le cas, et note chaque demande.
class ScriptedConfirmation final : public core::ConfirmationHandler {
  public:
    ScriptedConfirmation(core::Decision decision, std::vector<std::string>& asked)
        : decision_(decision), asked_(&asked) {}

    [[nodiscard]] core::Decision confirm(const core::ToolSpec& spec,
                                         const core::ToolCall& /*call*/) override {
        asked_->push_back(spec.name);
        return decision_;
    }

  private:
    core::Decision decision_;
    std::vector<std::string>* asked_;
};

/// Valeur d'une table de cas, sinon de celle par défaut.
const std::string* lookup(const std::map<std::string, std::string, std::less<>>& map,
                          std::string_view key) {
    const auto found = map.find(key);
    return found == map.end() ? nullptr : &found->second;
}

} // namespace

DoubleTool::DoubleTool(core::ToolSpec spec, std::string result, std::optional<std::string> failure)
    : spec_(std::move(spec)), result_(std::move(result)), failure_(std::move(failure)) {}

core::ToolResult DoubleTool::call(const nlohmann::json& arguments,
                                  const std::stop_token& /*stop*/) const {
    if (failure_) {
        return std::unexpected(
            core::ToolError{.kind = core::ToolErrorKind::Execution, .message = *failure_});
    }
    return core::ToolOutput{.content = fill_template(result_, arguments)};
}

Transcript run_case(const Case& test, const Suite& suite, std::span<const core::ToolSpec> specs,
                    std::unique_ptr<core::LlmProvider> llm, const RunOptions& options) {
    Transcript transcript;

    core::ToolRegistry tools;
    for (const auto& spec : specs) {
        const std::string* result = lookup(test.results, spec.name);
        if (result == nullptr) {
            result = lookup(suite.results, spec.name);
        }
        const std::string* failure = lookup(test.failures, spec.name);
        auto tool = std::make_shared<DoubleTool>(
            spec, result != nullptr ? *result : std::string{"Fait."},
            failure != nullptr ? std::optional<std::string>{*failure} : std::nullopt);
        if (auto added = tools.add(std::move(tool)); !added) {
            transcript.error = core::describe(added.error());
            return transcript;
        }
    }

    core::AgentOptions agent_options;
    agent_options.policy = test.policy.value_or(options.policy);
    agent_options.system_prompt = options.system_prompt;
    agent_options.max_steps = options.max_steps;
    core::Agent agent(
        std::move(llm), std::move(tools),
        std::make_unique<ScriptedConfirmation>(test.confirmation, transcript.confirmations),
        std::move(agent_options));

    const auto start = std::chrono::steady_clock::now();
    for (const auto& request : test.requests) {
        auto reply = agent.handle(request);
        if (!reply) {
            transcript.error = core::describe(reply.error());
            break;
        }
        transcript.reply = std::move(*reply);
    }
    transcript.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    for (const auto& message : agent.history()) {
        const auto* assistant = std::get_if<core::AssistantMessage>(&message);
        if (assistant == nullptr) {
            continue;
        }
        std::ranges::copy(assistant->tool_calls, std::back_inserter(transcript.calls));
        if (assistant->usage) {
            transcript.input_tokens += assistant->usage->input_tokens;
            transcript.output_tokens += assistant->usage->output_tokens;
            if (assistant->usage->cost_usd) {
                transcript.cost_usd = transcript.cost_usd.value_or(0) + *assistant->usage->cost_usd;
            }
        }
    }
    return transcript;
}

} // namespace avenoir::evals
