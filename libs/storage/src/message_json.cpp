#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "avenoir/storage/store.hpp"

namespace avenoir::storage::detail {

namespace {

using nlohmann::json;

core::Result<core::Message> malformed(const std::string& what) {
    return core::make_error(core::ErrorCode::Config, "message enregistré illisible : " + what);
}

bool is_string(const json& object, const char* key) {
    return object.contains(key) && object[key].is_string();
}

} // namespace

json message_to_json(const core::Message& message) {
    return std::visit(
        [](const auto& m) -> json {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, core::UserMessage>) {
                return {{"role", "user"}, {"content", m.content}};
            } else if constexpr (std::is_same_v<T, core::AssistantMessage>) {
                json calls = json::array();
                for (const auto& call : m.tool_calls) {
                    calls.push_back(
                        {{"id", call.id}, {"name", call.name}, {"arguments", call.arguments}});
                }
                json out = {
                    {"role", "assistant"}, {"text", m.text}, {"tool_calls", std::move(calls)}};
                if (m.usage) {
                    out["usage"] = {{"input_tokens", m.usage->input_tokens},
                                    {"output_tokens", m.usage->output_tokens}};
                    if (m.usage->cost_usd) {
                        out["usage"]["cost_usd"] = *m.usage->cost_usd;
                    }
                }
                return out;
            } else {
                return {{"role", "tool_result"},
                        {"call_id", m.call_id},
                        {"content", m.content},
                        {"is_error", m.is_error}};
            }
        },
        message);
}

core::Result<core::Message> message_from_json(const json& object) {
    if (!object.is_object() || !is_string(object, "role")) {
        return malformed("rôle absent");
    }
    const std::string role = object["role"].get<std::string>();

    if (role == "user") {
        if (!is_string(object, "content")) {
            return malformed("contenu absent");
        }
        return core::UserMessage{.content = object["content"].get<std::string>()};
    }

    if (role == "assistant") {
        if (!is_string(object, "text") || !object.contains("tool_calls") ||
            !object["tool_calls"].is_array()) {
            return malformed("réponse incomplète");
        }
        core::AssistantMessage message{.text = object["text"].get<std::string>(), .tool_calls = {}};
        for (const auto& call : object["tool_calls"]) {
            if (!is_string(call, "id") || !is_string(call, "name") || !call.contains("arguments")) {
                return malformed("appel d'outil incomplet");
            }
            message.tool_calls.push_back(core::ToolCall{.id = call["id"].get<std::string>(),
                                                        .name = call["name"].get<std::string>(),
                                                        .arguments = call["arguments"]});
        }
        if (object.contains("usage") && object["usage"].is_object()) {
            const auto& usage = object["usage"];
            core::Usage value;
            if (usage.contains("input_tokens") && usage["input_tokens"].is_number_unsigned()) {
                value.input_tokens = usage["input_tokens"].get<std::uint64_t>();
            }
            if (usage.contains("output_tokens") && usage["output_tokens"].is_number_unsigned()) {
                value.output_tokens = usage["output_tokens"].get<std::uint64_t>();
            }
            if (usage.contains("cost_usd") && usage["cost_usd"].is_number()) {
                value.cost_usd = usage["cost_usd"].get<double>();
            }
            message.usage = value;
        }
        return message;
    }

    if (role == "tool_result") {
        if (!is_string(object, "call_id") || !is_string(object, "content") ||
            !object.contains("is_error") || !object["is_error"].is_boolean()) {
            return malformed("résultat d'outil incomplet");
        }
        return core::ToolResultMessage{.call_id = object["call_id"].get<std::string>(),
                                       .content = object["content"].get<std::string>(),
                                       .is_error = object["is_error"].get<bool>()};
    }

    return malformed("rôle inconnu « " + role + " »");
}

} // namespace avenoir::storage::detail
