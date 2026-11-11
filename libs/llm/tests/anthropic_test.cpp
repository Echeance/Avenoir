#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/llm/anthropic.hpp"

using namespace avenoir;
using nlohmann::json;

TEST_CASE("anthropic : fusion des messages consécutifs de même rôle", "[anthropic]") {
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "salut"},
        core::AssistantMessage{
            .text = "Je regarde.",
            .tool_calls = {core::ToolCall{.id = "t1", .name = "x", .arguments = json::object()}}},
        core::ToolResultMessage{.call_id = "t1", .content = "ok", .is_error = false},
        core::UserMessage{.content = "et ensuite ?"},
    };
    const core::AnthropicConfig config{.model = "claude-sonnet-5-5",
                                       .api_key_env = "ANTHROPIC_API_KEY",
                                       .max_tokens = 1024,
                                       .base_url = std::nullopt};
    const core::LlmRequest request{
        .system = "Tu es Avenoir.", .messages = messages, .tools = {}, .stop = {}};

    const json body = llm::detail::anthropic_request_body(config, request);

    CHECK(body["system"] == "Tu es Avenoir.");
    CHECK(body["max_tokens"] == 1024);
    REQUIRE(body["messages"].size() == 3);
    CHECK(body["messages"][1]["content"].size() == 2); // texte + tool_use
    CHECK(body["messages"][1]["content"][1]["type"] == "tool_use");
    // Résultat d'outil et nouvelle question fusionnés dans un seul message user.
    REQUIRE(body["messages"][2]["content"].size() == 2);
    CHECK(body["messages"][2]["content"][0]["type"] == "tool_result");
    CHECK(body["messages"][2]["content"][1]["text"] == "et ensuite ?");
    CHECK_FALSE(body.contains("tools"));
}

TEST_CASE("anthropic : lecture des blocs texte et tool_use", "[anthropic]") {
    const json body = json::parse(R"({
        "content": [
            {"type": "text", "text": "Je lance "},
            {"type": "text", "text": "Firefox."},
            {"type": "tool_use", "id": "toolu_1", "name": "launch_app", "input": {"name": "firefox"}}
        ],
        "usage": {"input_tokens": 50, "output_tokens": 20}
    })");
    const auto response = llm::detail::parse_anthropic_response(body);
    REQUIRE(response.has_value());
    CHECK(response->text == "Je lance Firefox.");
    REQUIRE(response->tool_calls.size() == 1);
    CHECK(response->tool_calls[0] == core::ToolCall{.id = "toolu_1",
                                                    .name = "launch_app",
                                                    .arguments = json{{"name", "firefox"}}});
    CHECK(response->usage == core::Usage{.input_tokens = 50, .output_tokens = 20});
}

TEST_CASE("anthropic : réponse mal formée", "[anthropic]") {
    CHECK_FALSE(llm::detail::parse_anthropic_response(json::object()).has_value());
}

TEST_CASE("anthropic : réponse en flux, texte et outil", "[anthropic]") {
    llm::detail::AnthropicStream stream;
    std::vector<std::string> fragments;
    const core::TextSink sink = [&fragments](std::string_view text) {
        fragments.emplace_back(text);
    };
    for (
        const char* data : {
            R"({"type":"message_start","message":{"usage":{"input_tokens":20,"output_tokens":1}}})",
            R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})",
            R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Je lance "}})",
            R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Firefox."}})",
            R"({"type":"content_block_stop","index":0})",
            R"({"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"t1","name":"launch_app","input":{}}})",
            R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"name\": \"fire"}})",
            R"({"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"fox\"}"}})",
            R"({"type":"content_block_stop","index":1})",
            R"({"type":"message_delta","delta":{"stop_reason":"tool_use"},"usage":{"output_tokens":15}})",
            R"({"type":"message_stop"})",
        }) {
        REQUIRE(stream.feed(data, sink).has_value());
    }
    CHECK(fragments == std::vector<std::string>{"Je lance ", "Firefox."});
    const auto response = stream.finish();
    REQUIRE(response.has_value());
    CHECK(response->text == "Je lance Firefox.");
    REQUIRE(response->tool_calls.size() == 1);
    CHECK(response->tool_calls[0].id == "t1");
    CHECK(response->tool_calls[0].arguments == json{{"name", "firefox"}});
    REQUIRE(response->usage.has_value());
    if (response->usage) {
        CHECK(response->usage->input_tokens == 20);
        CHECK(response->usage->output_tokens == 15);
    }
}

TEST_CASE("anthropic : flux en erreur", "[anthropic]") {
    llm::detail::AnthropicStream stream;
    const auto error = stream.feed(
        R"({"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}})", {});
    REQUIRE_FALSE(error.has_value());
    CHECK(error.error().message == "Overloaded");
    llm::detail::AnthropicStream empty;
    CHECK_FALSE(empty.finish().has_value());
}
