#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/agent.hpp"
#include "avenoir/llm/models.hpp"

using namespace avenoir;

namespace {

/// Fournisseur qui répond par son nom : on voit quel modèle a répondu.
class NamedLlm final : public core::LlmProvider {
  public:
    explicit NamedLlm(std::string name) : name_(std::move(name)) {}
    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& /*request*/) override {
        return core::LlmResponse{.text = "réponse de " + name_, .tool_calls = {}};
    }

  private:
    std::string name_;
};

class CountingConfirmer final : public core::ConfirmationHandler {
  public:
    explicit CountingConfirmer(core::Decision decision) : decision_(decision) {}
    [[nodiscard]] core::Decision confirm(const core::ToolSpec& spec,
                                         const core::ToolCall& /*call*/) override {
        ++asked;
        last_risk = spec.risk;
        return decision_;
    }
    int asked = 0;
    core::RiskLevel last_risk = core::RiskLevel::ReadOnly;

  private:
    core::Decision decision_;
};

core::OpenAiCompatibleConfig ollama(std::string model,
                                    std::string url = "http://localhost:11434/v1") {
    return {.base_url = std::move(url),
            .model = std::move(model),
            .api_key_env = {},
            .temperature = {},
            .reasoning_effort = {}};
}

std::vector<llm::ModelChoice> sample_models() {
    return {
        {.name = "gemma", .config = ollama("gemma4:26b-a4b"), .local = true},
        {.name = "leger", .config = ollama("ministral-3:8b"), .local = true},
        {.name = "claude",
         .config =
             core::ClaudeCodeConfig{.model = "haiku", .command = "claude", .timeout_secs = 60},
         .local = false},
    };
}

llm::ProviderFactory fake_factory() {
    return [](const core::LlmConfig& config) -> core::Result<std::unique_ptr<core::LlmProvider>> {
        return std::make_unique<NamedLlm>(llm::model_id(config));
    };
}

core::Agent make_agent() {
    return {std::make_unique<NamedLlm>("gemma4:26b-a4b"), core::ToolRegistry{},
            std::make_unique<CountingConfirmer>(core::Decision::Rejected)};
}

} // namespace

TEST_CASE("local ou distant", "[models]") {
    CHECK(llm::is_local(ollama("m"), std::nullopt));
    CHECK(llm::is_local(ollama("m", "http://127.0.0.1:11434/v1"), std::nullopt));
    CHECK(llm::is_local(ollama("m", "http://[::1]:11434/v1"), std::nullopt));
    CHECK(llm::is_local(ollama("m", "HTTP://LocalHost/v1"), std::nullopt));
    CHECK_FALSE(llm::is_local(ollama("m", "https://api.mistral.ai/v1"), std::nullopt));
    CHECK_FALSE(llm::is_local(ollama("m", "http://192.168.1.15:11434/v1"), std::nullopt));
    // Piège : « localhost » dans l'identifiant ou le chemin, pas dans l'hôte.
    CHECK_FALSE(llm::is_local(ollama("m", "https://localhost@pirate.example/v1"), std::nullopt));
    CHECK_FALSE(llm::is_local(ollama("m", "https://pirate.example/localhost"), std::nullopt));
    CHECK_FALSE(llm::is_local(ollama("m", "https://localhost.pirate.example/v1"), std::nullopt));
    // Serveur du réseau local déclaré local ; les fournisseurs distants le restent.
    CHECK(llm::is_local(ollama("m", "http://192.168.1.15:11434/v1"), true));
    core::AnthropicConfig anthropic;
    anthropic.model = "m";
    CHECK_FALSE(llm::is_local(anthropic, std::nullopt));
}

TEST_CASE("modèles de la configuration et recherche par nom", "[models]") {
    core::AppConfig config;
    config.models.push_back({.name = "gemma", .config = ollama("gemma4:26b-a4b"), .local = {}});
    config.models.push_back({.name = "claude", .config = core::ClaudeCodeConfig{}, .local = {}});
    const auto models = llm::models_from(config);
    REQUIRE(models.size() == 2);
    CHECK(models[0].local);
    CHECK_FALSE(models[1].local);
    CHECK(llm::describe(models[1]) == "claude : modèle par défaut (Claude Code), distant");

    const llm::ModelSwitcher switcher(sample_models(), nullptr, fake_factory());
    CHECK(switcher.find("Léger") == &switcher.models()[1]);
    CHECK(switcher.find("MINISTRAL-3:8B") == &switcher.models()[1]);
    CHECK(switcher.find("gpt") == nullptr);
    CHECK(switcher.names() == "gemma, leger, claude");
}

TEST_CASE("changer de modèle : la conversation continue avec le nouveau", "[models]") {
    auto agent = make_agent();
    CountingConfirmer confirmer(core::Decision::Approved);
    llm::ModelSwitcher switcher(sample_models(), &confirmer, fake_factory());
    switcher.attach(agent);
    std::vector<std::string> remembered;
    switcher.on_switch(
        [&remembered](const llm::ModelChoice& model) { remembered.push_back(model.name); });

    REQUIRE(agent.handle("bonjour").value_or("") == "réponse de gemma4:26b-a4b");
    const auto switched = switcher.switch_to("leger");
    REQUIRE(switched.has_value());
    CHECK(switched->contains("ministral-3:8b"));
    CHECK(switcher.active()->name == "leger");
    CHECK(confirmer.asked == 0); // modèle local : pas de confirmation
    CHECK(agent.handle("et toi ?").value_or("") == "réponse de ministral-3:8b");
    CHECK(agent.history().size() == 4);
    CHECK(remembered == std::vector<std::string>{"leger"});

    const auto unknown = switcher.switch_to("gpt");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message.contains("gemma, leger, claude"));
    CHECK(switcher.active()->name == "leger");
}

TEST_CASE("passer à un modèle distant demande confirmation", "[models][securite]") {
    auto agent = make_agent();
    CountingConfirmer refuses(core::Decision::Rejected);
    llm::ModelSwitcher switcher(sample_models(), &refuses, fake_factory());
    switcher.attach(agent);

    const auto refused = switcher.switch_to("claude");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refuses.asked == 1);
    CHECK(refuses.last_risk == core::RiskLevel::Sensitive);
    CHECK(switcher.active()->name == "gemma");
    CHECK(agent.handle("bonjour").value_or("") == "réponse de gemma4:26b-a4b");

    // Sans moyen de confirmer : toujours refusé.
    llm::ModelSwitcher silent(sample_models(), nullptr, fake_factory());
    silent.attach(agent);
    CHECK_FALSE(silent.switch_to("claude").has_value());

    CountingConfirmer accepts(core::Decision::Approved);
    llm::ModelSwitcher approved(sample_models(), &accepts, fake_factory());
    approved.attach(agent);
    REQUIRE(approved.switch_to("claude").has_value());
    CHECK(accepts.asked == 1);
}

TEST_CASE("outil switch_llm : annulable, sans effet si déjà actif", "[models]") {
    auto agent = make_agent();
    CountingConfirmer confirmer(core::Decision::Approved);
    llm::ModelSwitcher switcher(sample_models(), &confirmer, fake_factory());
    switcher.attach(agent);
    const auto tool = llm::make_switch_llm_tool(switcher);

    const auto spec = tool->spec();
    CHECK(spec.name == "switch_llm");
    CHECK(spec.risk == core::RiskLevel::Reversible);
    CHECK(spec.description.contains("leger : ministral-3:8b, local"));
    CHECK(spec.description.contains("distant"));

    const auto same = tool->call({{"name", "gemma"}}, {});
    REQUIRE(same.has_value());
    CHECK(same->content.contains("déjà actif"));
    CHECK_FALSE(same->undo.has_value());

    const auto changed = tool->call({{"name", "leger"}}, {});
    REQUIRE(changed.has_value());
    if (const auto& undo = changed->undo) {
        CHECK(undo->description == "revenir au modèle « gemma »");
        REQUIRE(undo->run().has_value());
    } else {
        FAIL("annulation absente");
    }
    CHECK(switcher.active()->name == "gemma");

    // L'annulation n'écrase pas un choix fait entre-temps.
    const auto again = tool->call({{"name", "leger"}}, {});
    REQUIRE(again.has_value());
    REQUIRE(switcher.switch_to("claude").has_value());
    if (const auto& undo = again->undo) {
        CHECK_FALSE(undo->run().has_value());
    } else {
        FAIL("annulation absente");
    }
    CHECK(switcher.active()->name == "claude");

    CHECK_FALSE(tool->call({{"name", "gpt"}}, {}).has_value());
    CHECK_FALSE(tool->call({{"autre", 1}}, {}).has_value());
}

TEST_CASE("voie rapide : changer de modèle", "[models]") {
    core::FastRouter router;
    REQUIRE(llm::register_switch_rules(router).has_value());
    const auto name_of = [&router](std::string_view input) -> std::string {
        const auto route = router.route(input);
        if (!route || route->tool != "switch_llm") {
            return "";
        }
        return route->arguments.value("name", "");
    };
    CHECK(name_of("passe sur le modèle léger") == "léger");
    CHECK(name_of("Bascule vers le modèle claude") == "claude");
    CHECK(name_of("utilise le llm gemma") == "gemma");
    CHECK(name_of("change pour le modèle ministral-3:8b") == "ministral-3:8b");
    // Strictes : sans « modèle », la demande part au LLM.
    CHECK(name_of("passe à la suite").empty());
    CHECK(name_of("utilise Claude").empty());
    CHECK(name_of("passe sur le modèle léger et lance firefox").empty());
}

TEST_CASE("modèle de départ : le dernier utilisé, sinon le premier", "[models]") {
    llm::ModelSwitcher switcher(sample_models(), nullptr, fake_factory());
    auto provider = switcher.start("leger");
    REQUIRE(provider.has_value());
    REQUIRE(*provider != nullptr);
    CHECK((*provider)->name() == "ministral-3:8b");
    CHECK(switcher.active()->name == "leger");

    // Modèle retenu disparu de la configuration : le premier.
    provider = switcher.start("ancien");
    REQUIRE(provider.has_value());
    CHECK(switcher.active()->name == "gemma");
    provider = switcher.start(std::nullopt);
    CHECK(switcher.active()->name == "gemma");

    // Distant retenu : repris sans confirmation (choisi par l'utilisateur).
    REQUIRE(switcher.start("claude").has_value());
    CHECK(switcher.active()->name == "claude");

    // Aucun modèle : pas de fournisseur, seule la voie rapide répondra.
    llm::ModelSwitcher none({}, nullptr, fake_factory());
    const auto empty = none.start("gemma");
    REQUIRE(empty.has_value());
    CHECK(*empty == nullptr);
    CHECK(none.active() == nullptr);
    CHECK_FALSE(none.switch_to("gemma").has_value());
}
