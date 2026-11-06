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
