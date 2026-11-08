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

namespace {

struct Sent {
    std::string url;
    nlohmann::json body;
};

llm::JsonPoster recording_poster(std::vector<Sent>& sent) {
    return [&sent](const std::string& url, const nlohmann::json& body,
                   const std::stop_token& /*stop*/) -> core::Result<nlohmann::json> {
        sent.push_back({.url = url, .body = body});
        return nlohmann::json::object();
    };
}

} // namespace

TEST_CASE("charger, décharger, recharger un modèle local", "[models]") {
    auto agent = make_agent();
    std::vector<Sent> sent;
    llm::ModelSwitcher switcher(sample_models(), nullptr, fake_factory(), recording_poster(sent));
    switcher.attach(agent);
    REQUIRE(switcher.start("gemma").has_value());

    // Un autre modèle que l'actif : tout de suite.
    const auto loaded = switcher.set_residency("leger", llm::Residency::Load, {});
    REQUIRE(loaded.has_value());
    CHECK(loaded->contains("chargé"));
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].url == "http://localhost:11434/api/generate");
    CHECK(sent[0].body == nlohmann::json{{"model", "ministral-3:8b"}});

    REQUIRE(switcher.set_residency("leger", llm::Residency::Unload, {}).has_value());
    REQUIRE(sent.size() == 2);
    CHECK(sent[1].body == nlohmann::json{{"model", "ministral-3:8b"}, {"keep_alive", 0}});

    REQUIRE(switcher.set_residency("leger", llm::Residency::Reload, {}).has_value());
    REQUIRE(sent.size() == 4); // déchargé puis chargé
    CHECK(sent[2].body.contains("keep_alive"));
    CHECK_FALSE(sent[3].body.contains("keep_alive"));

    // Le modèle actif : différé à la fin de la requête (sa réponse le rechargerait).
    const auto deferred = switcher.set_residency("", llm::Residency::Unload, {});
    REQUIRE(deferred.has_value());
    CHECK(deferred->contains("à la fin de cette réponse"));
    CHECK(sent.size() == 4);
    const auto done = switcher.finish_request();
    CHECK(done.value_or("").contains("déchargé"));
    REQUIRE(sent.size() == 5);
    CHECK(sent[4].body == nlohmann::json{{"model", "gemma4:26b-a4b"}, {"keep_alive", 0}});
    CHECK_FALSE(switcher.finish_request().has_value()); // plus rien en attente

    // Rechargé avant la fin de la requête : le déchargement différé est annulé.
    REQUIRE(switcher.set_residency("gemma", llm::Residency::Unload, {}).has_value());
    REQUIRE(switcher.set_residency("gemma", llm::Residency::Load, {}).has_value());
    CHECK_FALSE(switcher.finish_request().has_value());

    // Modèle distant ou inconnu : refusé, rien envoyé.
    const auto before = sent.size();
    CHECK_FALSE(switcher.set_residency("claude", llm::Residency::Load, {}).has_value());
    CHECK_FALSE(switcher.set_residency("gpt", llm::Residency::Unload, {}).has_value());
    CHECK(sent.size() == before);
}

TEST_CASE("outils et voie rapide pour charger et décharger", "[models]") {
    auto agent = make_agent();
    std::vector<Sent> sent;
    llm::ModelSwitcher switcher(sample_models(), nullptr, fake_factory(), recording_poster(sent));
    switcher.attach(agent);
    REQUIRE(switcher.start(std::nullopt).has_value());
    const auto tools = llm::make_residency_tools(switcher);
    REQUIRE(tools.size() == 2);
    CHECK(tools[0]->spec().name == "unload_llm");
    CHECK(tools[1]->spec().name == "load_llm");
    CHECK(tools[1]->spec().input_schema["properties"].contains("reload"));

    REQUIRE(tools[1]->call({{"name", "leger"}, {"reload", true}}, {}).has_value());
    CHECK(sent.size() == 2);
    CHECK(tools[0]->call(nlohmann::json::object(), {})->content.contains("à la fin"));
    CHECK_FALSE(tools[0]->call({{"name", "claude"}}, {}).has_value());

    core::FastRouter router;
    REQUIRE(llm::register_residency_rules(router).has_value());
    const auto route = [&router](std::string_view input) {
        return router.route(input).value_or(core::FastRoute{.tool = "", .arguments = {}});
    };
    CHECK(route("décharge le modèle").tool == "unload_llm");
    CHECK(route("Stoppe le modèle gemma").arguments == nlohmann::json{{"name", "gemma"}});
    CHECK(route("libère la carte graphique").tool == "unload_llm");
    CHECK(route("allume le modèle leger").arguments == nlohmann::json{{"name", "leger"}});
    CHECK(route("recharge le modèle").arguments == nlohmann::json{{"reload", true}});
    CHECK(route("recharge le llm gemma").arguments ==
          nlohmann::json{{"reload", true}, {"name", "gemma"}});
    CHECK(route("arrête la musique").tool.empty());
    CHECK(route("charge mon téléphone").tool.empty());
}

namespace {

/// Fournisseur qui retient l'effort de raisonnement de sa configuration.
class EffortLlm final : public core::LlmProvider {
  public:
    explicit EffortLlm(std::optional<std::string> effort) : effort_(std::move(effort)) {}
    [[nodiscard]] std::string_view name() const override { return "effort"; }
    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& /*request*/) override {
        return core::LlmResponse{.text = "effort " + effort_.value_or("défaut"), .tool_calls = {}};
    }

  private:
    std::optional<std::string> effort_;
};

llm::ProviderFactory effort_factory(int& created) {
    return [&created](
               const core::LlmConfig& config) -> core::Result<std::unique_ptr<core::LlmProvider>> {
        ++created;
        const auto* openai = std::get_if<core::OpenAiCompatibleConfig>(&config);
        return std::make_unique<EffortLlm>(openai != nullptr ? openai->reasoning_effort
                                                             : std::nullopt);
    };
}

} // namespace

TEST_CASE("changer le raisonnement sans recharger le modèle", "[models]") {
    auto agent = make_agent();
    int created = 0;
    llm::ModelSwitcher switcher(sample_models(), nullptr, effort_factory(created));
    switcher.restore_reasoning("gemma", "none");           // retenu au lancement précédent
    switcher.restore_reasoning("gemma", "n'importe quoi"); // ignoré
    auto provider = switcher.start("gemma");
    REQUIRE(provider.has_value());
    agent.set_llm(std::move(*provider));
    switcher.attach(agent);
    std::vector<std::string> remembered;
    switcher.on_reasoning([&remembered](const llm::ModelChoice& model) {
        remembered.push_back(model.name + "=" + llm::ModelSwitcher::reasoning(model).value_or(""));
    });
    CHECK(agent.handle("a").value_or("") == "effort none");

    const auto high = switcher.set_reasoning("", "Élevé");
    REQUIRE(high.has_value());
    CHECK(*high == "Raisonnement de « gemma » : élevé.");
    CHECK(agent.handle("b").value_or("") == "effort high"); // dès la réponse suivante
    CHECK(agent.history().size() == 4);                     // conversation gardée

    // Un autre modèle que l'actif : réglage gardé pour quand il servira.
    const int before = created;
    REQUIRE(switcher.set_reasoning("leger", "low").has_value());
    CHECK(created == before);
    CHECK(llm::ModelSwitcher::reasoning(*switcher.find("leger")) == "low");

    REQUIRE(switcher.set_reasoning("gemma", "défaut").has_value());
    CHECK(agent.handle("c").value_or("") == "effort défaut");
    CHECK(remembered == std::vector<std::string>{"gemma=high", "leger=low", "gemma="});

    CHECK_FALSE(switcher.set_reasoning("", "maximum").has_value());
    CHECK_FALSE(switcher.set_reasoning("claude", "high").has_value()); // claude_code
    CHECK_FALSE(switcher.set_reasoning("gpt", "high").has_value());
}

TEST_CASE("outil et voie rapide du raisonnement", "[models]") {
    auto agent = make_agent();
    int created = 0;
    llm::ModelSwitcher switcher(sample_models(), nullptr, effort_factory(created));
    REQUIRE(switcher.start("gemma").has_value());
    switcher.attach(agent);
    const auto tool = llm::make_reasoning_tool(switcher);
    CHECK(tool->spec().name == "set_reasoning");

    const auto set = tool->call({{"level", "élevé"}}, {});
    REQUIRE(set.has_value());
    if (const auto& undo = set->undo) {
        CHECK(undo->description == "remettre le raisonnement de « gemma » à par défaut (choix du "
                                   "serveur)");
        REQUIRE(undo->run().has_value());
        CHECK_FALSE(llm::ModelSwitcher::reasoning(*switcher.find("gemma")).has_value());
    } else {
        FAIL("annulation absente");
    }
    CHECK_FALSE(tool->call({{"level", "énorme"}}, {}).has_value());

    core::FastRouter router;
    REQUIRE(llm::register_reasoning_rules(router).has_value());
    const auto args = [&router](std::string_view input) {
        const auto route = router.route(input);
        return route && route->tool == "set_reasoning" ? route->arguments : nlohmann::json{};
    };
    CHECK(args("mets le raisonnement à élevé") == nlohmann::json{{"level", "élevé"}});
    CHECK(args("raisonnement faible") == nlohmann::json{{"level", "faible"}});
    CHECK(args("désactive le raisonnement") == nlohmann::json{{"level", "aucun"}});
    CHECK(args("active le raisonnement") == nlohmann::json{{"level", "moyen"}});
    CHECK(args("mets le son à fond").is_null());
}

TEST_CASE("changer de modèle décharge l'ancien s'il partage le serveur", "[models]") {
    auto models = sample_models();
    models.push_back({.name = "autre",
                      .config = ollama("qwen3.8:27b", "http://192.168.1.20:11434/v1"),
                      .local = true});
    models.push_back({.name = "jumeau",
                      .config = ollama("gemma4:26b-a4b", "http://LOCALHOST:11434/v1/"),
                      .local = true});
    auto agent = make_agent();
    std::vector<Sent> sent;
    CountingConfirmer confirmer(core::Decision::Approved);
    llm::ModelSwitcher switcher(models, &confirmer, fake_factory(), recording_poster(sent));
    switcher.attach(agent);
    REQUIRE(switcher.start("gemma").has_value());

    // Même serveur, autre modèle : l'ancien est déchargé après la requête.
    const auto to_light = switcher.switch_to("leger");
    REQUIRE(to_light.has_value());
    CHECK(to_light->contains("L'ancien modèle « gemma » sera déchargé."));
    CHECK(sent.empty());
    CHECK(switcher.finish_request().value_or("").contains("« gemma » déchargé"));
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].body == nlohmann::json{{"model", "gemma4:26b-a4b"}, {"keep_alive", 0}});

    // Retour avant la fin de la requête : il sert de nouveau, rien n'est déchargé.
    REQUIRE(switcher.switch_to("gemma").has_value());
    REQUIRE(switcher.switch_to("leger").has_value());
    CHECK(switcher.finish_request().value_or("").contains("« gemma » déchargé"));
    REQUIRE(switcher.switch_to("gemma").has_value()); // leger en attente…
    REQUIRE(switcher.switch_to("leger").has_value()); // … puis de nouveau actif
    const auto pending = switcher.finish_request().value_or("");
    CHECK_FALSE(pending.contains("« leger »"));

    // Autre serveur : le décharger ne libérerait rien pour le nouveau.
    sent.clear();
    const auto remote_server = switcher.switch_to("autre");
    REQUIRE(remote_server.has_value());
    CHECK_FALSE(remote_server->contains("déchargé"));
    CHECK_FALSE(switcher.finish_request().has_value());

    // Même serveur (adresse écrite autrement) et même modèle : rien à décharger.
    REQUIRE(switcher.switch_to("gemma").has_value());
    (void)switcher.finish_request();
    sent.clear();
    CHECK_FALSE(switcher.switch_to("jumeau")->contains("déchargé"));

    // Vers un modèle distant : l'ancien n'est pas sur le même serveur.
    CHECK_FALSE(switcher.switch_to("claude")->contains("déchargé"));
    CHECK_FALSE(switcher.finish_request().has_value());
    CHECK(sent.empty());

    // Réglage désactivé : l'ancien reste chargé.
    switcher.set_unload_on_switch(false);
    REQUIRE(switcher.switch_to("gemma").has_value());
    CHECK_FALSE(switcher.switch_to("leger")->contains("déchargé"));
    CHECK_FALSE(switcher.finish_request().has_value());
}

TEST_CASE("savoir si un modèle est chargé (Ollama /api/ps)", "[models]") {
    nlohmann::json listed{{"models", nlohmann::json::array({{{"name", "gemma4:26b-a4b"}}})}};
    std::string asked;
    const llm::JsonGetter getter =
        [&listed, &asked](const std::string& url) -> core::Result<nlohmann::json> {
        asked = url;
        if (listed.is_null()) {
            return core::make_error(core::ErrorCode::Llm, "injoignable");
        }
        return listed;
    };
    auto models = sample_models();
    models.push_back({.name = "sans-tag", .config = ollama("llama3"), .local = true});
    const llm::ModelSwitcher switcher(models, nullptr, fake_factory(), {}, getter);

    CHECK(switcher.is_loaded(*switcher.find("gemma")) == true);
    CHECK(asked == "http://localhost:11434/api/ps");
    CHECK(switcher.is_loaded(*switcher.find("leger")) == false);
    CHECK_FALSE(switcher.is_loaded(*switcher.find("claude")).has_value()); // distant

    listed["models"].push_back({{"model", "llama3:latest"}});
    CHECK(switcher.is_loaded(*switcher.find("sans-tag")) == true);

    listed = nlohmann::json{{"autre", 1}}; // pas Ollama : on ne sait pas
    CHECK_FALSE(switcher.is_loaded(*switcher.find("gemma")).has_value());
    listed = nullptr; // injoignable
    CHECK_FALSE(switcher.is_loaded(*switcher.find("gemma")).has_value());
}

TEST_CASE("état de chargement lisible, outil et voie rapide", "[models]") {
    nlohmann::json listed{{"models", nlohmann::json::array({{{"name", "gemma4:26b-a4b"},
                                                             {"size_vram", 19700000000ULL}}})}};
    const llm::JsonGetter getter =
        [&listed](const std::string& /*url*/) -> core::Result<nlohmann::json> { return listed; };
    llm::ModelSwitcher switcher(sample_models(), nullptr, fake_factory(), {}, getter);
    REQUIRE(switcher.start("gemma").has_value());

    const auto all = switcher.status("");
    REQUIRE(all.has_value());
    CHECK(*all == "gemma : chargé (19,7 Go de mémoire vidéo), actif.\n"
                  "leger : pas chargé.\n"
                  "claude : distant (rien à charger sur cette machine).");
    CHECK(switcher.status("Léger").value_or("") == "leger : pas chargé.");
    CHECK_FALSE(switcher.status("gpt").has_value());

    const auto tool = llm::make_status_tool(switcher);
    CHECK(tool->spec().risk == core::RiskLevel::ReadOnly);
    const auto one = tool->call({{"name", "gemma"}}, {});
    REQUIRE(one.has_value());
    CHECK(one->content.starts_with("gemma : chargé"));

    listed = nlohmann::json{{"pas", "ollama"}};
    CHECK(switcher.status("gemma").value_or("").contains("état inconnu"));

    core::FastRouter router;
    REQUIRE(llm::register_status_rules(router).has_value());
    const auto args = [&router](std::string_view input) {
        const auto route = router.route(input);
        return route && route->tool == "llm_status" ? route->arguments : nlohmann::json{};
    };
    CHECK(args("quels modèles sont chargés ?") == nlohmann::json::object());
    CHECK(args("Quel modèle est chargé") == nlohmann::json::object());
    CHECK(args("état des modèles") == nlohmann::json::object());
    CHECK(args("le modèle gemma est-il chargé ?") == nlohmann::json{{"name", "gemma"}});
    CHECK(args("est-ce que le modèle qwen est chargé") == nlohmann::json{{"name", "qwen"}});
    CHECK(args("quels fichiers sont chargés").is_null());
}

TEST_CASE("mode local uniquement : aucun modèle distant", "[models][securite]") {
    auto agent = make_agent();
    CountingConfirmer confirmer(core::Decision::Approved);
    llm::ModelSwitcher switcher(sample_models(), &confirmer, fake_factory());
    switcher.set_local_only(true);
    switcher.attach(agent);

    // Distant retenu au lancement précédent : le premier modèle local à la place.
    REQUIRE(switcher.start("claude").has_value());
    CHECK(switcher.active()->name == "gemma");

    const auto refused = switcher.switch_to("claude");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.contains("local uniquement"));
    CHECK(confirmer.asked == 0); // refusé sans même demander
    CHECK(switcher.active()->name == "gemma");
    CHECK(switcher.switch_to("leger").has_value()); // local : permis

    // Aucun modèle local : pas de fournisseur, seule la voie rapide répond.
    std::vector<llm::ModelChoice> remote_only{sample_models()[2]};
    llm::ModelSwitcher none(remote_only, &confirmer, fake_factory());
    none.set_local_only(true);
    const auto provider = none.start(std::nullopt);
    REQUIRE(provider.has_value());
    CHECK(*provider == nullptr);
    CHECK(none.active() == nullptr);
}
