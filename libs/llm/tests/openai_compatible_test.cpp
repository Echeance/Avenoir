#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/llm/openai_compatible.hpp"

using namespace avenoir;
using nlohmann::json;

namespace {

core::LlmRequest make_request(const std::vector<core::Message>& messages,
                              const std::vector<core::ToolSpec>& tools) {
    return core::LlmRequest{
        .system = "Tu es Avenoir.", .messages = messages, .tools = tools, .stop = {}};
}

} // namespace

TEST_CASE("openai : requête avec système, historique et outils", "[openai]") {
    const std::vector<core::Message> messages{
        core::UserMessage{.content = "quelle heure ?"},
        core::AssistantMessage{.text = {},
                               .tool_calls = {core::ToolCall{.id = "c1",
                                                             .name = "current_time",
                                                             .arguments = json::object()}}},
        core::ToolResultMessage{.call_id = "c1", .content = "Il est 13 h.", .is_error = false},
    };
    const std::vector<core::ToolSpec> tools{core::ToolSpec{.name = "current_time",
                                                           .description = "Heure locale",
                                                           .input_schema = json::object(),
                                                           .risk = core::RiskLevel::ReadOnly}};
    const core::OpenAiCompatibleConfig config{.base_url = "http://localhost:11434/v1",
                                              .model = "qwen3:8b",
                                              .api_key_env = std::nullopt,
                                              .temperature = 0.2,
                                              .reasoning_effort = "none"};

    const json body = llm::detail::openai_request_body(config, make_request(messages, tools));

    CHECK(body["model"] == "qwen3:8b");
    CHECK(body["temperature"] == 0.2);
    CHECK(body["reasoning_effort"] == "none");
    REQUIRE(body["messages"].size() == 4);
    CHECK(body["messages"][0] == json{{"role", "system"}, {"content", "Tu es Avenoir."}});
    CHECK(body["messages"][2]["content"].is_null());
    CHECK(body["messages"][2]["tool_calls"][0]["function"]["arguments"] == "{}");
    CHECK(body["messages"][3] ==
          json{{"role", "tool"}, {"tool_call_id", "c1"}, {"content", "Il est 13 h."}});
    CHECK(body["tools"][0]["function"]["name"] == "current_time");
}

TEST_CASE("openai : pas d'outils ni de température si absents", "[openai]") {
    const std::vector<core::Message> messages{core::UserMessage{.content = "salut"}};
    const core::OpenAiCompatibleConfig config{.base_url = "u",
                                              .model = "m",
                                              .api_key_env = std::nullopt,
                                              .temperature = std::nullopt,
                                              .reasoning_effort = std::nullopt};
    const json body = llm::detail::openai_request_body(config, make_request(messages, {}));
    CHECK_FALSE(body.contains("tools"));
    CHECK_FALSE(body.contains("temperature"));
    CHECK_FALSE(body.contains("reasoning_effort"));
}

TEST_CASE("openai : lecture d'une réponse texte avec consommation", "[openai]") {
    const json body = json::parse(R"({
        "choices": [{"message": {"role": "assistant", "content": "Bonjour !"}}],
        "usage": {"prompt_tokens": 12, "completion_tokens": 3}
    })");
    const auto response = llm::detail::parse_openai_response(body);
    REQUIRE(response.has_value());
    CHECK(response->text == "Bonjour !");
    CHECK(response->tool_calls.empty());
    CHECK(response->usage == core::Usage{.input_tokens = 12, .output_tokens = 3});
}

TEST_CASE("openai : appels d'outils (arguments en chaîne ou en objet)", "[openai]") {
    const json body = json::parse(R"({
        "choices": [{"message": {"content": null, "tool_calls": [
            {"id": "a", "function": {"name": "launch_app", "arguments": "{\"name\":\"firefox\"}"}},
            {"function": {"name": "current_time", "arguments": {}}},
            {"id": "c", "function": {"name": "open_url", "arguments": "pas du json"}}
        ]}}]
    })");
    const auto response = llm::detail::parse_openai_response(body);
    REQUIRE(response.has_value());
    CHECK(response->text.empty());
    REQUIRE(response->tool_calls.size() == 3);
    CHECK(response->tool_calls[0] ==
          core::ToolCall{.id = "a", .name = "launch_app", .arguments = json{{"name", "firefox"}}});
    CHECK(response->tool_calls[1].id == "call_1");
    // Arguments illisibles transmis tels quels : l'outil les refusera.
    CHECK(response->tool_calls[2].arguments == "pas du json");
}

TEST_CASE("openai : réponse mal formée", "[openai]") {
    CHECK_FALSE(llm::detail::parse_openai_response(json::object()).has_value());
    CHECK_FALSE(llm::detail::parse_openai_response(json{{"choices", json::array()}}).has_value());
}
