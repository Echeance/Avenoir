#include <cstddef>
#include <deque>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/agent.hpp"

namespace {

using namespace avenoir::core;

/// LLM factice qui renvoie des réponses préparées, dans l'ordre.
class ScriptedLlm final : public LlmProvider {
  public:
    explicit ScriptedLlm(std::deque<Result<LlmResponse>> script, std::shared_ptr<int> calls)
        : script_(std::move(script)), calls_(std::move(calls)) {}

    [[nodiscard]] std::string_view name() const override { return "scripted"; }

    [[nodiscard]] Result<LlmResponse> complete(const LlmRequest& /*request*/) override {
        ++*calls_;
        if (script_.empty()) {
            return make_error(ErrorCode::Llm, "script épuisé");
        }
        auto next = std::move(script_.front());
        script_.pop_front();
        return next;
    }

  private:
    std::deque<Result<LlmResponse>> script_;
    std::shared_ptr<int> calls_;
};

/// Outil qui compte ses appels et renvoie un texte fixe.
class CountingTool final : public Tool {
  public:
    CountingTool(std::string name, RiskLevel risk, std::shared_ptr<int> calls,
                 std::string output = "ok")
        : name_(std::move(name)), risk_(risk), calls_(std::move(calls)),
          output_(std::move(output)) {}

    [[nodiscard]] ToolSpec spec() const override {
        return ToolSpec{.name = name_,
                        .description = "outil de test",
                        .input_schema = nlohmann::json::object(),
                        .risk = risk_};
    }
    [[nodiscard]] ToolResult call(const nlohmann::json& /*arguments*/) const override {
        ++*calls_;
        return ToolOutput{.content = output_};
    }

  private:
    std::string name_;
    RiskLevel risk_;
    std::shared_ptr<int> calls_;
    std::string output_;
};

/// Confirmation qui accepte tout.
class AutoApprove final : public ConfirmationHandler {
  public:
    [[nodiscard]] Decision confirm(const ToolSpec& /*spec*/, const ToolCall& /*call*/) override {
        return Decision::Approved;
    }
};

LlmResponse call_tool(std::string name) {
    return LlmResponse{
        .text = {},
        .tool_calls = {
            ToolCall{.id = "1", .name = std::move(name), .arguments = nlohmann::json::object()}}};
}

LlmResponse answer(std::string text) {
    return LlmResponse{.text = std::move(text), .tool_calls = {}};
}

struct Fixture {
    std::shared_ptr<int> llm_calls = std::make_shared<int>(0);
    std::shared_ptr<int> tool_calls = std::make_shared<int>(0);

    [[nodiscard]] Agent make_agent(std::deque<Result<LlmResponse>> script, RiskLevel risk,
                                   std::unique_ptr<ConfirmationHandler> confirmer,
                                   AgentOptions options = {},
                                   std::string tool_output = "ok") const {
        auto tool =
            std::make_shared<CountingTool>("flag", risk, tool_calls, std::move(tool_output));
        ToolRegistry tools;
        const auto added = tools.add(std::move(tool));
        REQUIRE(added.has_value());
        return {std::make_unique<ScriptedLlm>(std::move(script), llm_calls), std::move(tools),
                std::move(confirmer), std::move(options)};
    }
};

const ToolResultMessage& tool_result_at(const Agent& agent, std::size_t index) {
    const auto* result = std::get_if<ToolResultMessage>(&agent.history()[index]);
    REQUIRE(result != nullptr);
    return *result;
}

} // namespace

TEST_CASE("exécute un outil puis donne la réponse finale", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("flag"), answer("terminé")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());

    const auto reply = agent.handle("vas-y");
    REQUIRE(reply.has_value());
    CHECK(*reply == "terminé");
    CHECK(*f.tool_calls == 1);
    CHECK(*f.llm_calls == 2);
    // Utilisateur, Assistant (appel), Résultat, Assistant (réponse).
    REQUIRE(agent.history().size() == 4);
    CHECK_FALSE(tool_result_at(agent, 2).is_error);
}

TEST_CASE("une action sensible refusée n'est pas exécutée", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("flag"), answer("d'accord")}, RiskLevel::Sensitive,
                              std::make_unique<AutoReject>());

    const auto reply = agent.handle("supprime tout");
    REQUIRE(reply.has_value());
    CHECK(*f.tool_calls == 0);
    CHECK(tool_result_at(agent, 2).is_error);
}

TEST_CASE("une action sensible confirmée est exécutée", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("flag"), answer("fait")}, RiskLevel::Sensitive,
                              std::make_unique<AutoApprove>());

    REQUIRE(agent.handle("vas-y").has_value());
    CHECK(*f.tool_calls == 1);
}

TEST_CASE("la politique deny bloque même avec confirmation", "[agent]") {
    const Fixture f;
    AgentOptions options;
    options.policy.read_only = Policy::Deny;
    auto agent = f.make_agent({call_tool("flag"), answer("refusé")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoApprove>(), std::move(options));

    REQUIRE(agent.handle("vas-y").has_value());
    CHECK(*f.tool_calls == 0);
    CHECK(tool_result_at(agent, 2).is_error);
}

TEST_CASE("un outil inconnu renvoie une erreur au LLM", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("inexistant"), answer("désolé")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());

    const auto reply = agent.handle("vas-y");
    REQUIRE(reply.has_value());
    CHECK(*reply == "désolé");
    CHECK(tool_result_at(agent, 2).is_error);
}

TEST_CASE("max_steps arrête une boucle sans fin", "[agent]") {
    const Fixture f;
    AgentOptions options;
    options.max_steps = 3;
    auto agent =
        f.make_agent({call_tool("flag"), call_tool("flag"), call_tool("flag"), call_tool("flag")},
                     RiskLevel::ReadOnly, std::make_unique<AutoReject>(), std::move(options));

    const auto reply = agent.handle("boucle");
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().code == ErrorCode::MaxStepsReached);
    CHECK(*f.llm_calls == 3);
}

TEST_CASE("une erreur du LLM est remontée", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({make_error(ErrorCode::Llm, "réseau")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());

    const auto reply = agent.handle("bonjour");
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().code == ErrorCode::Llm);
}

TEST_CASE("les résultats trop longs sont tronqués sans couper un caractère", "[agent]") {
    const Fixture f;
    // 'é' fait 2 octets en UTF-8 : une coupe au milieu donnerait un texte invalide.
    std::string long_output;
    for (std::size_t i = 0; i < kMaxToolOutputChars + 10; ++i) {
        long_output += "é";
    }
    auto agent = f.make_agent({call_tool("flag"), answer("ok")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>(), {}, long_output);

    REQUIRE(agent.handle("vas-y").has_value());
    const std::string& content = tool_result_at(agent, 2).content;
    const std::string marker = "\n[… résultat tronqué]";
    REQUIRE(content.ends_with(marker));
    CHECK(content.size() - marker.size() == kMaxToolOutputChars * 2);
}

TEST_CASE("la voie rapide n'appelle pas le LLM", "[agent]") {
    const Fixture f;
    auto router = FastRouter{};
    REQUIRE(router.add_rule("^drapeau$", "flag").has_value());
    AgentOptions options;
    options.router = std::move(router);
    auto agent = f.make_agent({}, RiskLevel::ReadOnly, std::make_unique<AutoReject>(),
                              std::move(options), "Drapeau levé.");

    const auto reply = agent.handle("Drapeau !");
    REQUIRE(reply.has_value());
    CHECK(*reply == "Drapeau levé.");
    CHECK(*f.llm_calls == 0);
    CHECK(*f.tool_calls == 1);
    CHECK(agent.history().size() == 2);
}

TEST_CASE("la voie rapide respecte les permissions", "[agent]") {
    const Fixture f;
    auto router = FastRouter{};
    REQUIRE(router.add_rule("^drapeau$", "flag").has_value());
    AgentOptions options;
    options.router = std::move(router);
    auto agent =
        f.make_agent({}, RiskLevel::Sensitive, std::make_unique<AutoReject>(), std::move(options));

    REQUIRE(agent.handle("drapeau").has_value());
    CHECK(*f.tool_calls == 0);
}

TEST_CASE("une requête annulée s'arrête avant d'appeler le LLM", "[agent]") {
    const Fixture f;
    auto agent =
        f.make_agent({answer("jamais")}, RiskLevel::ReadOnly, std::make_unique<AutoReject>());

    // Non const : request_stop() est non const dans la norme (MSVC le refuse sur
    // un objet const ; libstdc++ l'accepte, d'où la fausse alerte de clang-tidy).
    std::stop_source source; // NOLINT(misc-const-correctness)
    source.request_stop();
    const auto reply = agent.handle("bonjour", source.get_token());
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().code == ErrorCode::Cancelled);
    CHECK(*f.llm_calls == 0);
}

TEST_CASE("reset oublie la conversation", "[agent]") {
    const Fixture f;
    auto agent =
        f.make_agent({answer("salut")}, RiskLevel::ReadOnly, std::make_unique<AutoReject>());
    REQUIRE(agent.handle("bonjour").has_value());
    CHECK_FALSE(agent.history().empty());
    agent.reset();
    CHECK(agent.history().empty());
}

TEST_CASE("une réponse finale vide est une erreur, pas une ligne blanche", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({answer("")}, RiskLevel::ReadOnly, std::make_unique<AutoReject>());

    const auto reply = agent.handle("bonjour");
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().code == ErrorCode::Llm);
    CHECK(agent.history().empty());
}

TEST_CASE("une réponse vide après des outils garde l'historique", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("flag"), answer("")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());

    const auto reply = agent.handle("vas-y");
    REQUIRE_FALSE(reply.has_value());
    CHECK(*f.tool_calls == 1);
    // Utilisateur, Assistant (appel), Résultat : l'action a eu lieu.
    CHECK(agent.history().size() == 3);
}
