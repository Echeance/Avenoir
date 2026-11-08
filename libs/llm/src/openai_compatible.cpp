#include "avenoir/llm/openai_compatible.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include "http.hpp"
#include "json_util.hpp"

namespace avenoir::llm {

namespace {

using nlohmann::json;

json to_wire_message(const core::Message& message) {
    return std::visit(
        [](const auto& m) -> json {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, core::UserMessage>) {
                return {{"role", "user"}, {"content", m.content}};
            } else if constexpr (std::is_same_v<T, core::AssistantMessage>) {
                json out = {{"role", "assistant"}};
                // Avec des appels d'outils, un texte vide s'écrit null.
                out["content"] =
                    m.text.empty() && !m.tool_calls.empty() ? json(nullptr) : json(m.text);
                if (!m.tool_calls.empty()) {
                    json calls = json::array();
                    for (const auto& call : m.tool_calls) {
                        calls.push_back(
                            {{"id", call.id},
                             {"type", "function"},
                             {"function",
                              {{"name", call.name}, {"arguments", call.arguments.dump()}}}});
                    }
                    out["tool_calls"] = std::move(calls);
                }
                return out;
            } else {
                return {{"role", "tool"}, {"tool_call_id", m.call_id}, {"content", m.content}};
            }
        },
        message);
}

/// Les arguments arrivent normalement en chaîne JSON, parfois (Ollama) en
/// objet. Une chaîne illisible est transmise telle quelle : l'outil la
/// refusera et l'erreur reviendra au LLM.
json parse_arguments(const json& raw) {
    if (raw.is_object()) {
        return raw;
    }
    if (raw.is_string()) {
        const auto text = raw.get<std::string>();
        if (text.empty()) {
            return json::object();
        }
        json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
        return parsed.is_discarded() ? raw : parsed;
    }
    return json::object();
}

} // namespace

namespace detail {

json openai_request_body(const core::OpenAiCompatibleConfig& config,
                         const core::LlmRequest& request) {
    json messages = json::array();
    if (!request.system.empty()) {
        messages.push_back({{"role", "system"}, {"content", request.system}});
    }
    for (const auto& message : request.messages) {
        messages.push_back(to_wire_message(message));
    }

    json body = {{"model", config.model}, {"messages", std::move(messages)}};
    if (!request.tools.empty()) {
        json tools = json::array();
        for (const auto& tool : request.tools) {
            tools.push_back({{"type", "function"},
                             {"function",
                              {{"name", tool.name},
                               {"description", tool.description},
                               {"parameters", tool.input_schema}}}});
        }
        body["tools"] = std::move(tools);
    }
    if (config.reasoning_effort) {
        body["reasoning_effort"] = *config.reasoning_effort;
    }
    if (config.temperature) {
        body["temperature"] = *config.temperature;
    }
    return body;
}

core::Result<core::LlmResponse> parse_openai_response(const json& body) {
    const auto* choices = field(body, "choices");
    if (choices == nullptr || !choices->is_array() || choices->empty()) {
        return core::make_error(core::ErrorCode::Llm, "réponse sans « choices »");
    }
    const auto* message = field(choices->front(), "message");
    if (message == nullptr || !message->is_object()) {
        return core::make_error(core::ErrorCode::Llm, "réponse sans « message »");
    }

    core::LlmResponse response;
    response.text = string_field(*message, "content").value_or("");

    if (const auto* calls = field(*message, "tool_calls"); calls != nullptr && calls->is_array()) {
        std::size_t index = 0;
        for (const auto& call : *calls) {
            const auto* function = field(call, "function");
            const auto name = function != nullptr ? string_field(*function, "name") : std::nullopt;
            if (!name) {
                continue;
            }
            const auto* arguments = field(*function, "arguments");
            response.tool_calls.push_back(core::ToolCall{
                .id = string_field(call, "id").value_or("call_" + std::to_string(index)),
                .name = *name,
                .arguments = arguments != nullptr ? parse_arguments(*arguments) : json::object(),
            });
            ++index;
        }
    }

    if (const auto* usage = field(body, "usage"); usage != nullptr) {
        response.usage = core::Usage{
            .input_tokens = unsigned_field(*usage, "prompt_tokens").value_or(0),
            .output_tokens = unsigned_field(*usage, "completion_tokens").value_or(0),
        };
    }
    return response;
}

} // namespace detail

OpenAiCompatible::OpenAiCompatible(core::OpenAiCompatibleConfig config,
                                   std::optional<std::string> api_key)
    : config_(std::move(config)), api_key_(std::move(api_key)) {}

core::Result<core::LlmResponse> OpenAiCompatible::complete(const core::LlmRequest& request) {
    std::string url = config_.base_url;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    detail::HttpRequest http{.url = url + "/chat/completions",
                             .headers = {},
                             .body = detail::openai_request_body(config_, request).dump()};
    if (api_key_) {
        http.headers.emplace_back("Authorization", "Bearer " + *api_key_);
    }
    auto body = detail::post_json(http, request.stop);
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    return detail::parse_openai_response(*body);
}

} // namespace avenoir::llm
