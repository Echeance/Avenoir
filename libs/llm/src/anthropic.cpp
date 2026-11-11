#include "avenoir/llm/anthropic.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "http.hpp"
#include "json_util.hpp"

namespace avenoir::llm {

namespace {

using nlohmann::json;

constexpr std::string_view kDefaultBaseUrl = "https://api.anthropic.com";
constexpr std::string_view kApiVersion = "2023-06-01";

struct WireMessage {
    std::string role;
    json blocks;
};

/// Rôle et blocs de contenu d'un message (vide si rien à envoyer).
WireMessage to_blocks(const core::Message& message) {
    return std::visit(
        [](const auto& m) -> WireMessage {
            using T = std::decay_t<decltype(m)>;
            json blocks = json::array();
            if constexpr (std::is_same_v<T, core::UserMessage>) {
                blocks.push_back({{"type", "text"}, {"text", m.content}});
                return {.role = "user", .blocks = std::move(blocks)};
            } else if constexpr (std::is_same_v<T, core::AssistantMessage>) {
                if (!m.text.empty()) {
                    blocks.push_back({{"type", "text"}, {"text", m.text}});
                }
                for (const auto& call : m.tool_calls) {
                    blocks.push_back({{"type", "tool_use"},
                                      {"id", call.id},
                                      {"name", call.name},
                                      {"input", call.arguments}});
                }
                return {.role = "assistant", .blocks = std::move(blocks)};
            } else {
                blocks.push_back({{"type", "tool_result"},
                                  {"tool_use_id", m.call_id},
                                  {"content", m.content},
                                  {"is_error", m.is_error}});
                return {.role = "user", .blocks = std::move(blocks)};
            }
        },
        message);
}

} // namespace

namespace detail {

json anthropic_request_body(const core::AnthropicConfig& config, const core::LlmRequest& request) {
    // Fusion des messages consécutifs de même rôle (ex. plusieurs résultats
    // d'outils suivis d'une nouvelle question de l'utilisateur).
    std::vector<WireMessage> merged;
    for (const auto& message : request.messages) {
        WireMessage wire = to_blocks(message);
        if (wire.blocks.empty()) {
            continue;
        }
        if (!merged.empty() && merged.back().role == wire.role) {
            for (auto& block : wire.blocks) {
                merged.back().blocks.push_back(std::move(block));
            }
        } else {
            merged.push_back(std::move(wire));
        }
    }

    json messages = json::array();
    for (auto& wire : merged) {
        messages.push_back({{"role", wire.role}, {"content", std::move(wire.blocks)}});
    }

    json body = {
        {"model", config.model},
        {"max_tokens", config.max_tokens},
        {"messages", std::move(messages)},
    };
    if (!request.system.empty()) {
        body["system"] = request.system;
    }
    if (!request.tools.empty()) {
        json tools = json::array();
        for (const auto& tool : request.tools) {
            tools.push_back({{"name", tool.name},
                             {"description", tool.description},
                             {"input_schema", tool.input_schema}});
        }
        body["tools"] = std::move(tools);
    }
    return body;
}

core::Result<core::LlmResponse> parse_anthropic_response(const json& body) {
    const auto* content = field(body, "content");
    if (content == nullptr || !content->is_array()) {
        return core::make_error(core::ErrorCode::Llm, "réponse sans « content »");
    }

    core::LlmResponse response;
    for (const auto& block : *content) {
        const auto type = string_field(block, "type");
        if (type == "text") {
            response.text += string_field(block, "text").value_or("");
        } else if (type == "tool_use") {
            const auto* input = field(block, "input");
            response.tool_calls.push_back(core::ToolCall{
                .id = string_field(block, "id").value_or(""),
                .name = string_field(block, "name").value_or(""),
                .arguments = input != nullptr && input->is_object() ? *input : json::object(),
            });
        }
    }

    if (const auto* usage = field(body, "usage"); usage != nullptr) {
        response.usage = core::Usage{
            .input_tokens = unsigned_field(*usage, "input_tokens").value_or(0),
            .output_tokens = unsigned_field(*usage, "output_tokens").value_or(0),
        };
    }
    return response;
}

core::Result<void> AnthropicStream::feed(std::string_view data, const core::TextSink& sink) {
    const json event = json::parse(data, nullptr, /*allow_exceptions=*/false);
    if (event.is_discarded() || !event.is_object()) {
        return core::make_error(core::ErrorCode::Llm, "évènement de flux illisible");
    }
    received_ = true;
    const auto type = string_field(event, "type").value_or("");
    const auto block_at = [this, &event] -> Block* {
        const auto index = unsigned_field(event, "index");
        if (!index || *index > 64) {
            return nullptr;
        }
        if (*index >= blocks_.size()) {
            blocks_.resize(static_cast<std::size_t>(*index) + 1);
        }
        return &blocks_[static_cast<std::size_t>(*index)];
    };
    if (type == "error") {
        const auto* error = field(event, "error");
        const auto message = error != nullptr ? string_field(*error, "message") : std::nullopt;
        return core::make_error(core::ErrorCode::Llm, message.value_or("erreur de l'API"));
    }
    if (type == "message_start") {
        const auto* message = field(event, "message");
        if (const auto* usage = message != nullptr ? field(*message, "usage") : nullptr) {
            response_.usage = core::Usage{
                .input_tokens = unsigned_field(*usage, "input_tokens").value_or(0),
                .output_tokens = unsigned_field(*usage, "output_tokens").value_or(0),
            };
        }
    } else if (type == "content_block_start") {
        const auto* content = field(event, "content_block");
        auto* block = block_at();
        if (block != nullptr && content != nullptr &&
            string_field(*content, "type") == "tool_use") {
            block->tool = true;
            block->id = string_field(*content, "id").value_or("");
            block->name = string_field(*content, "name").value_or("");
        }
    } else if (type == "content_block_delta") {
        const auto* delta = field(event, "delta");
        auto* block = block_at();
        if (delta == nullptr || block == nullptr) {
            return {};
        }
        const auto kind = string_field(*delta, "type").value_or("");
        if (kind == "text_delta") {
            const auto text = string_field(*delta, "text").value_or("");
            response_.text += text;
            if (sink && !text.empty()) {
                sink(text);
            }
        } else if (kind == "input_json_delta") {
            block->input += string_field(*delta, "partial_json").value_or("");
        }
    } else if (type == "message_delta") {
        if (const auto* usage = field(event, "usage"); usage != nullptr) {
            if (!response_.usage) {
                response_.usage = core::Usage{};
            }
            response_.usage->output_tokens =
                unsigned_field(*usage, "output_tokens").value_or(response_.usage->output_tokens);
        }
    }
    return {};
}

core::Result<core::LlmResponse> AnthropicStream::finish() {
    if (!received_) {
        return core::make_error(core::ErrorCode::Llm, "flux vide");
    }
    for (auto& block : blocks_) {
        if (!block.tool) {
            continue;
        }
        json input = block.input.empty()
                         ? json::object()
                         : json::parse(block.input, nullptr, /*allow_exceptions=*/false);
        response_.tool_calls.push_back(core::ToolCall{
            .id = std::move(block.id),
            .name = std::move(block.name),
            .arguments = input.is_object() ? std::move(input) : json::object(),
        });
    }
    blocks_.clear();
    return std::move(response_);
}

} // namespace detail

Anthropic::Anthropic(core::AnthropicConfig config, std::string api_key)
    : config_(std::move(config)), api_key_(std::move(api_key)) {}

core::Result<core::LlmResponse> Anthropic::complete(const core::LlmRequest& request) {
    std::string url = config_.base_url.value_or(std::string(kDefaultBaseUrl));
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    detail::HttpRequest http{
        .url = url + "/v1/messages",
        .headers = {{"x-api-key", api_key_}, {"anthropic-version", std::string(kApiVersion)}},
        .body = detail::anthropic_request_body(config_, request).dump(),
    };
    if (request.on_text) {
        json body = detail::anthropic_request_body(config_, request);
        body["stream"] = true;
        http.body = body.dump();
        detail::AnthropicStream stream;
        auto streamed = detail::post_stream(
            http,
            [&stream, &request](std::string_view data) {
                return stream.feed(data, request.on_text);
            },
            request.stop);
        if (!streamed) {
            return std::unexpected(std::move(streamed.error()));
        }
        return stream.finish();
    }
    auto body = detail::post_json(http, request.stop);
    if (!body) {
        return std::unexpected(std::move(body.error()));
    }
    return detail::parse_anthropic_response(*body);
}

} // namespace avenoir::llm
