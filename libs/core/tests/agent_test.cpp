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
    [[nodiscard]] ToolResult call(const nlohmann::json& /*arguments*/,
                                  const std::stop_token& /*stop*/) const override {
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

// ─── Annulation des actions ─────────────────────────────────────────────────

namespace {

/// Outil annulable : met `*value` à l'argument « to ». Son annulation remet
/// l'ancienne valeur, seulement si personne ne l'a changée entre-temps.
class SetValueTool final : public Tool {
  public:
    explicit SetValueTool(std::shared_ptr<int> value) : value_(std::move(value)) {}

    [[nodiscard]] ToolSpec spec() const override {
        return ToolSpec{.name = "set_value",
                        .description = "test",
                        .input_schema = nlohmann::json::object(),
                        .risk = RiskLevel::Reversible};
    }
    [[nodiscard]] ToolResult call(const nlohmann::json& arguments,
                                  const std::stop_token& /*stop*/) const override {
        const int previous = *value_;
        const int next = arguments.value("to", 0);
        *value_ = next;
        const auto value = value_;
        return ToolOutput{
            .content = "ok",
            .undo = UndoAction{.description = "remettre la valeur à " + std::to_string(previous),
                               .run = [value, previous, next] -> ToolResult {
                                   if (*value != next) {
                                       return std::unexpected(
                                           ToolError{.kind = ToolErrorKind::Execution,
                                                     .message = "la valeur a changé"});
                                   }
                                   *value = previous;
                                   return ToolOutput{.content = "valeur remise"};
                               }}};
    }

  private:
    std::shared_ptr<int> value_;
};

LlmResponse set_value(int to, std::string id = "1") {
    return LlmResponse{.text = {},
                       .tool_calls = {ToolCall{.id = std::move(id),
                                               .name = "set_value",
                                               .arguments = nlohmann::json{{"to", to}}}}};
}

struct UndoFixture {
    std::shared_ptr<int> value = std::make_shared<int>(0);
    std::shared_ptr<int> llm_calls = std::make_shared<int>(0);
    std::shared_ptr<int> flag_calls = std::make_shared<int>(0);

    [[nodiscard]] Agent make_agent(std::deque<Result<LlmResponse>> script) const {
        ToolRegistry tools;
        REQUIRE(tools.add(std::make_shared<SetValueTool>(value)).has_value());
        REQUIRE(tools.add(std::make_shared<CountingTool>("flag", RiskLevel::Reversible, flag_calls))
                    .has_value());
        return {std::make_unique<ScriptedLlm>(std::move(script), llm_calls), std::move(tools),
                std::make_unique<AutoReject>(), AgentOptions{}};
    }
};

} // namespace

TEST_CASE("annulation : ordre inverse et non annulables listées", "[agent][undo]") {
    const UndoFixture f;
    auto agent =
        f.make_agent({set_value(1, "a"), set_value(2, "b"), call_tool("flag"), answer("fait")});

    REQUIRE(agent.handle("vas-y").has_value());
    CHECK(*f.value == 2);

    const RequestJournal* journal = agent.last_undoable();
    REQUIRE(journal != nullptr);
    CHECK(journal->request == "vas-y");
    REQUIRE(journal->actions.size() == 3);
    CHECK(journal->undoable_count() == 2);
    CHECK_FALSE(journal->actions[2].undo.has_value()); // « flag » n'est pas annulable

    const auto outcomes = agent.undo_last();
    REQUIRE(outcomes.size() == 2);
    CHECK(outcomes[0].description == "remettre la valeur à 1"); // la dernière d'abord
    CHECK(outcomes[1].description == "remettre la valeur à 0");
    CHECK(outcomes[0].succeeded);
    CHECK(outcomes[1].succeeded);
    CHECK(*f.value == 0);
    CHECK(agent.last_undoable() == nullptr);
}

TEST_CASE("annulation : refusée si l'état a changé, sans bloquer les suivantes", "[agent][undo]") {
    const UndoFixture f;
    auto agent = f.make_agent({set_value(5), answer("fait")});
    REQUIRE(agent.handle("vas-y").has_value());

    *f.value = 99; // quelqu'un a changé la valeur entre-temps
    const auto outcomes = agent.undo_last();
    REQUIRE(outcomes.size() == 1);
    CHECK_FALSE(outcomes[0].succeeded);
    CHECK(outcomes[0].message.find("la valeur a changé") != std::string::npos);
    CHECK(*f.value == 99); // rien n'a été écrasé
}

TEST_CASE("annulation : /annuler répété remonte aux requêtes précédentes", "[agent][undo]") {
    const UndoFixture f;
    auto agent = f.make_agent({set_value(1), answer("un"), set_value(2), answer("deux")});
    REQUIRE(agent.handle("première").has_value());
    REQUIRE(agent.handle("seconde").has_value());

    REQUIRE(agent.last_undoable() != nullptr);
    CHECK(agent.last_undoable()->request == "seconde");
    CHECK(agent.undo_last().size() == 1);
    CHECK(*f.value == 1);

    REQUIRE(agent.last_undoable() != nullptr);
    CHECK(agent.last_undoable()->request == "première");
    CHECK(agent.undo_last().size() == 1);
    CHECK(*f.value == 0);
    CHECK(agent.undo_last().empty());
}

TEST_CASE("annulation : une requête interrompue garde son journal", "[agent][undo]") {
    const UndoFixture f;
    // Le LLM demande deux actions ; la seconde demande l'arrêt (comme un
    // Ctrl+C) : la requête est interrompue avant l'étape suivante.
    std::stop_source source; // NOLINT(misc-const-correctness) : request_stop() est non const
    struct StopOnCall final : public Tool {
        explicit StopOnCall(std::stop_source* s) : source(s) {}
        std::stop_source* source;
        [[nodiscard]] ToolSpec spec() const override {
            return ToolSpec{.name = "stop",
                            .description = "test",
                            .input_schema = nlohmann::json::object(),
                            .risk = RiskLevel::ReadOnly};
        }
        [[nodiscard]] ToolResult call(const nlohmann::json& /*arguments*/,
                                      const std::stop_token& /*stop*/) const override {
            source->request_stop();
            return ToolOutput{.content = "arrêt"};
        }
    };
    ToolRegistry tools;
    REQUIRE(tools.add(std::make_shared<SetValueTool>(f.value)).has_value());
    REQUIRE(tools.add(std::make_shared<StopOnCall>(&source)).has_value());
    const LlmResponse both{
        .text = {},
        .tool_calls = {
            ToolCall{.id = "1", .name = "set_value", .arguments = nlohmann::json{{"to", 7}}},
            ToolCall{.id = "2", .name = "stop", .arguments = nlohmann::json::object()}}};
    Agent interrupted(std::make_unique<ScriptedLlm>(
                          std::deque<Result<LlmResponse>>{both, answer("x")}, f.llm_calls),
                      std::move(tools), std::make_unique<AutoReject>());

    const auto reply = interrupted.handle("fais-le", source.get_token());
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().code == ErrorCode::Cancelled);
    CHECK(*f.value == 7);
    REQUIRE(interrupted.last_undoable() != nullptr);
    CHECK(interrupted.undo_last().size() == 1);
    CHECK(*f.value == 0);
}

TEST_CASE("annulation : les outils en lecture seule ne sont pas journalisés", "[agent][undo]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("flag"), answer("ok")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());
    REQUIRE(agent.handle("vas-y").has_value());
    CHECK(agent.last_undoable() == nullptr);
}

// ─── Historique : observateur et reprise ────────────────────────────────────

TEST_CASE("l'observateur voit chaque message ajouté, dans l'ordre", "[agent][history]") {
    const Fixture f;
    auto agent = f.make_agent({call_tool("flag"), answer("terminé")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());
    const auto seen = std::make_shared<std::vector<Message>>();
    agent.set_history_observer(HistoryObserver{
        .appended = [seen](const Message& message) { seen->push_back(message); },
        .removed_last = [seen] { seen->pop_back(); },
    });

    REQUIRE(agent.handle("vas-y").has_value());
    REQUIRE(seen->size() == agent.history().size());
    for (std::size_t i = 0; i < seen->size(); ++i) {
        CHECK(seen->at(i) == agent.history()[i]);
    }
}

TEST_CASE("l'observateur voit le retrait d'une question sans réponse", "[agent][history]") {
    const Fixture f;
    auto agent = f.make_agent({answer("")}, RiskLevel::ReadOnly, std::make_unique<AutoReject>());
    const auto seen = std::make_shared<std::vector<Message>>();
    agent.set_history_observer(HistoryObserver{
        .appended = [seen](const Message& message) { seen->push_back(message); },
        .removed_last = [seen] { seen->pop_back(); },
    });

    CHECK_FALSE(agent.handle("bonjour").has_value());
    CHECK(seen->empty());
}

TEST_CASE("load_history reprend une conversation", "[agent][history]") {
    const Fixture f;
    auto agent = f.make_agent({answer("Tu t'appelles Nathan.")}, RiskLevel::ReadOnly,
                              std::make_unique<AutoReject>());
    agent.load_history({UserMessage{.content = "Je m'appelle Nathan."},
                        AssistantMessage{.text = "Enchanté !", .tool_calls = {}}});

    REQUIRE(agent.handle("Comment je m'appelle ?").has_value());
    REQUIRE(agent.history().size() == 4);
    CHECK(std::get<UserMessage>(agent.history()[0]).content == "Je m'appelle Nathan.");
}

TEST_CASE("la consommation est gardée dans le message de l'assistant", "[agent][history]") {
    const Fixture f;
    auto response = answer("ok");
    response.usage = Usage{.input_tokens = 10, .output_tokens = 2};
    auto agent = f.make_agent({response}, RiskLevel::ReadOnly, std::make_unique<AutoReject>());
    REQUIRE(agent.handle("salut").has_value());
    const auto& reply = std::get<AssistantMessage>(agent.history().back());
    CHECK(reply.usage == Usage{.input_tokens = 10, .output_tokens = 2});
}

TEST_CASE("une commande exécute un outil sans LLM, sous contrôle des permissions", "[agent]") {
    const Fixture f;
    auto agent = f.make_agent({}, RiskLevel::ReadOnly, std::make_unique<AutoReject>(), {},
                              "résultat direct");
    const auto reply = agent.run_tool(
        "/flag", ToolCall{.id = {}, .name = "flag", .arguments = nlohmann::json::object()});
    REQUIRE(reply.has_value());
    CHECK(*reply == "résultat direct");
    CHECK(*f.tool_calls == 1);
    CHECK(*f.llm_calls == 0);
    REQUIRE(agent.history().size() == 2); // saisie + réponse, pour le contexte du LLM

    CHECK_FALSE(
        agent
            .run_tool("/absent",
                      ToolCall{.id = {}, .name = "absent", .arguments = nlohmann::json::object()})
            .has_value());

    // Sensible + refus : l'outil n'est pas exécuté.
    const Fixture g;
    auto guarded = g.make_agent({}, RiskLevel::Sensitive, std::make_unique<AutoReject>());
    const auto refused = guarded.run_tool(
        "/flag", ToolCall{.id = {}, .name = "flag", .arguments = nlohmann::json::object()});
    REQUIRE(refused.has_value());
    CHECK(*g.tool_calls == 0);
    CHECK(refused->contains("refusé"));
}

TEST_CASE("une commande interrompue est signalée comme telle, pas comme un échec", "[agent]") {
    // Comme un module dont l'appel est annulé par Ctrl+C : l'outil demande
    // l'arrêt et renvoie un échec sans message.
    std::stop_source source; // NOLINT(misc-const-correctness) : request_stop() est non const
    struct CancelledTool final : public Tool {
        explicit CancelledTool(std::stop_source* s) : source(s) {}
        std::stop_source* source;
        [[nodiscard]] ToolSpec spec() const override {
            return ToolSpec{.name = "slow",
                            .description = "test",
                            .input_schema = nlohmann::json::object(),
                            .risk = RiskLevel::ReadOnly};
        }
        [[nodiscard]] ToolResult call(const nlohmann::json& /*arguments*/,
                                      const std::stop_token& /*stop*/) const override {
            source->request_stop();
            return std::unexpected(ToolError{.kind = ToolErrorKind::Execution, .message = {}});
        }
    };
    ToolRegistry tools;
    REQUIRE(tools.add(std::make_shared<CancelledTool>(&source)).has_value());
    const auto llm_calls = std::make_shared<int>(0);
    Agent agent(std::make_unique<ScriptedLlm>(std::deque<Result<LlmResponse>>{}, llm_calls),
                std::move(tools), std::make_unique<AutoReject>());

    const auto reply = agent.run_tool(
        "/slow", ToolCall{.id = {}, .name = "slow", .arguments = nlohmann::json::object()},
        source.get_token());
    REQUIRE_FALSE(reply.has_value());
    CHECK(reply.error().code == ErrorCode::Cancelled);
    REQUIRE(agent.history().size() == 2);
    const auto& note = std::get<AssistantMessage>(agent.history().back());
    CHECK(note.text.contains("interrompue"));
    CHECK_FALSE(note.text.contains("échec"));
}

TEST_CASE("changer de LLM garde la conversation", "[agent]") {
    const Fixture f;
    auto agent =
        f.make_agent({answer("premier")}, RiskLevel::ReadOnly, std::make_unique<AutoReject>());
    REQUIRE(agent.handle("bonjour").has_value());

    const auto second_calls = std::make_shared<int>(0);
    agent.set_llm(std::make_unique<ScriptedLlm>(std::deque<Result<LlmResponse>>{answer("second")},
                                                second_calls));
    const auto reply = agent.handle("et maintenant ?");
    REQUIRE(reply.has_value());
    CHECK(*reply == "second");
    CHECK(*second_calls == 1);
    CHECK(agent.history().size() == 4); // les deux échanges

    agent.set_llm(nullptr); // ignoré : le fournisseur reste valide
    CHECK_FALSE(agent.llm_name().empty());
}
