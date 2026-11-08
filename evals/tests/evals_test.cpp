// Tests du banc d'essai, sans vrai LLM : lecture des cas, vérifications,
// doublures et passage d'un cas avec un LLM scripté.

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cases.hpp"
#include "report.hpp"
#include "runner.hpp"

using namespace avenoir;
using namespace avenoir::evals;
using nlohmann::json;

namespace {

class ScriptedLlm final : public core::LlmProvider {
  public:
    explicit ScriptedLlm(std::deque<core::LlmResponse> script) : script_(std::move(script)) {}

    [[nodiscard]] std::string_view name() const override { return "scripted"; }

    [[nodiscard]] core::Result<core::LlmResponse>
    complete(const core::LlmRequest& request) override {
        if (script_.empty()) {
            return core::make_error(core::ErrorCode::Llm, "script épuisé");
        }
        for (const auto& message : request.messages) {
            if (const auto* result = std::get_if<core::ToolResultMessage>(&message)) {
                last_result = result->content;
            }
        }
        auto next = std::move(script_.front());
        script_.pop_front();
        return next;
    }

    static inline std::string last_result;

  private:
    std::deque<core::LlmResponse> script_;
};

core::LlmResponse call(std::string name, json arguments) {
    return {
        .text = {},
        .tool_calls = {{.id = "c1", .name = std::move(name), .arguments = std::move(arguments)}},
        .usage = core::Usage{.input_tokens = 100, .output_tokens = 10, .cost_usd = 0.001}};
}

core::LlmResponse answer(std::string text) {
    return {.text = std::move(text), .tool_calls = {}, .usage = std::nullopt};
}

std::vector<core::ToolSpec> specs() {
    return {
        {.name = "launch_app",
         .description = "Lance une application",
         .input_schema = {{"type", "object"}},
         .risk = core::RiskLevel::Reversible},
        {.name = "open_url",
         .description = "Ouvre une adresse web",
         .input_schema = {{"type", "object"}},
         .risk = core::RiskLevel::Reversible},
        {.name = "meteo__current",
         .description = "Météo actuelle",
         .input_schema = {{"type", "object"}},
         .risk = core::RiskLevel::ReadOnly},
    };
}

} // namespace

TEST_CASE("le fichier de requêtes de référence est valide", "[evals]") {
    const auto suite = load_suite(AVENOIR_EVALS_CASES);
    REQUIRE(suite.has_value());
    CHECK(suite->cases.size() >= 20);
    CHECK(suite->results.contains("current_time"));
    for (const auto& test : suite->cases) {
        CHECK_FALSE(test.requests.empty());
    }
}

TEST_CASE("lecture des cas et erreurs signalées", "[evals]") {
    const auto suite = parse_suite(R"(
        [results]
        launch_app = "Lancé : {name}."

        [[cases]]
        id = "a"
        category = "simple"
        requests = ["un", "deux"]
        confirm = "accept"
        policy = { reversible = "confirm" }
        forbid = ["open_url"]
        max_calls = 2
        reply_contains = ["ok"]

        [[cases.expect]]
        tool = "launch_app"
        arguments = { name = "firefox", count = 2 }

        [cases.failures]
        open_url = "réseau coupé"
    )");
    REQUIRE(suite.has_value());
    REQUIRE(suite->cases.size() == 1);
    const Case& test = suite->cases.front();
    CHECK(test.requests == std::vector<std::string>{"un", "deux"});
    CHECK(test.confirmation == core::Decision::Approved);
    REQUIRE(test.policy.has_value());
    CHECK(test.policy.value_or(core::PermissionPolicy{}).reversible == core::Policy::Confirm);
    REQUIRE(test.expect.size() == 1);
    CHECK(test.expect[0].arguments == json{{"name", "firefox"}, {"count", 2}});
    CHECK(test.max_calls == 2U);
    CHECK(test.failures.at("open_url") == "réseau coupé");

    const auto error_of = [](const char* text) {
        const auto parsed = parse_suite(text);
        return parsed ? std::string{} : core::describe(parsed.error());
    };
    CHECK(error_of(R"([[cases]]
id = "a"
request = "x"
reponse = "y")")
              .contains("cases[0] (a).reponse : clé inconnue"));
    CHECK(error_of("[[cases]]\nid = \"a\"").contains("request ou requests obligatoire"));
    CHECK(error_of("[[cases]]\nid = \"a\"\nrequest = \"x\"\n[[cases]]\nid = \"a\"\nrequest = "
                   "\"y\"")
              .contains("identifiant en double"));
    CHECK(error_of("[[cases]]\nid = \"a\"\nrequest = \"x\"\nconfirm = \"oui\"")
              .contains("accept ou reject"));
    CHECK(error_of("[[cases]]\nid = \"a\"\nrequest = \"x\"\npolicy = { sensitive = \"peut-être\" }")
              .contains("policy.sensitive"));
    CHECK(error_of("[[cases]]\nid = \"a\"\nrequest = \"x\"\n"
                   "expect = [{ tool = \"t\", arguments = { a = [1] } }]")
              .contains("texte, nombre ou booléen"));
    CHECK(error_of("racine = 1").contains("clé inconnue"));
    CHECK(error_of("[[cases]\n").contains("ligne"));
}

TEST_CASE("modèles des résultats des doublures", "[evals]") {
    CHECK(fill_template("À {city} : 12 °C", {{"city", "Brest"}}) == "À Brest : 12 °C");
    CHECK(fill_template("À {city|Lyon} : 12 °C", json::object()) == "À Lyon : 12 °C");
    CHECK(fill_template("{days} jours", {{"days", 3}}) == "3 jours");
    CHECK(fill_template("{inconnu} {", json::object()) == "{inconnu} {");
    CHECK(fill_template("sans accolade", json::object()) == "sans accolade");
}

TEST_CASE("vérification d'un cas", "[evals]") {
    Case test;
    test.id = "t";
    test.expect = {{.tool = "launch_app", .arguments = {{"name", "Firefox"}}},
                   {.tool = "launch_app", .arguments = {{"name", "thunderbird"}}}};
    test.forbid = {"open_url"};
    test.forbid_in_arguments = {"rm -rf"};
    test.max_calls = 2;
    test.reply_contains = {"Lancées"};
    test.reply_excludes = {"erreur"};

    Transcript good;
    good.calls = {{.id = "1", .name = "launch_app", .arguments = {{"name", "firefox"}}},
                  {.id = "2", .name = "launch_app", .arguments = {{"name", "Thunderbird Mail"}}}};
    good.reply = "Applications lancées.";
    CHECK(check(test, good).empty());

    // Le même appel ne compte pas pour deux attentes.
    Transcript twice = good;
    twice.calls[1] = twice.calls[0];
    const auto missing = check(test, twice);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].contains("appel attendu absent : launch_app"));

    Transcript bad = good;
    bad.calls.push_back({.id = "3", .name = "open_url", .arguments = {{"url", "rm -rf ~"}}});
    bad.reply = "Une erreur est survenue.";
    bad.error = "délai dépassé";
    const auto failures = check(test, bad);
    CHECK(failures.size() == 6);

    Case accents;
    accents.reply_contains_any = {"météo", "temps"};
    Transcript upper;
    upper.reply = "MÉTÉO : beau.";
    CHECK(check(accents, upper).empty());
    upper.reply = "Rien.";
    CHECK(check(accents, upper).size() == 1);

    // Nombre attendu : comparaison numérique, pas de texte.
    Case days;
    days.expect = {{.tool = "f", .arguments = {{"days", 2}}}};
    Transcript as_text;
    as_text.calls = {{.id = "1", .name = "f", .arguments = {{"days", "2"}}}};
    CHECK(check(days, as_text).size() == 1);
    as_text.calls[0].arguments["days"] = 2.0;
    CHECK(check(days, as_text).empty());
}

TEST_CASE("passage d'un cas avec un LLM scripté et des doublures", "[evals]") {
    Suite suite;
    suite.results = {{"meteo__current", "À {city|Lyon} : 12 °C."}};

    Case test;
    test.id = "meteo";
    test.requests = {"Quel temps à Brest ?"};
    test.expect = {{.tool = "meteo__current", .arguments = {{"city", "Brest"}}}};

    const auto all = specs();
    auto transcript = run_case(
        test, suite, all,
        std::make_unique<ScriptedLlm>(std::deque{call("meteo__current", {{"city", "Brest"}}),
                                                 answer("Il fait 12 °C à Brest.")}),
        RunOptions{.system_prompt = "test", .max_steps = 5, .policy = {}});
    CHECK_FALSE(transcript.error.has_value());
    CHECK(ScriptedLlm::last_result == "À Brest : 12 °C.");
    CHECK(transcript.reply == "Il fait 12 °C à Brest.");
    CHECK(transcript.input_tokens == 100);
    CHECK(transcript.cost_usd == 0.001);
    CHECK(check(test, transcript).empty());

    // Refus de la confirmation : l'appel est noté, l'outil n'est pas exécuté.
    Case refused;
    refused.id = "refus";
    refused.requests = {"Lance Firefox"};
    refused.policy = core::PermissionPolicy{.read_only = core::Policy::Allow,
                                            .reversible = core::Policy::Confirm,
                                            .sensitive = core::Policy::Confirm};
    transcript =
        run_case(refused, suite, all,
                 std::make_unique<ScriptedLlm>(std::deque{call("launch_app", {{"name", "firefox"}}),
                                                          answer("D'accord, je ne le lance pas.")}),
                 RunOptions{.system_prompt = "test", .max_steps = 5, .policy = {}});
    CHECK(transcript.confirmations == std::vector<std::string>{"launch_app"});
    REQUIRE(transcript.calls.size() == 1);
    CHECK_FALSE(ScriptedLlm::last_result.contains("lancée"));

    // Échec préparé d'une doublure, puis erreur du fournisseur (script épuisé).
    Case failing;
    failing.id = "échec";
    failing.requests = {"Météo à Xyz ?", "Et demain ?"};
    failing.failures = {{"meteo__current", "ville introuvable"}};
    transcript =
        run_case(failing, suite, all,
                 std::make_unique<ScriptedLlm>(std::deque{call("meteo__current", {{"city", "Xyz"}}),
                                                          answer("Ville introuvable.")}),
                 RunOptions{.system_prompt = "test", .max_steps = 5, .policy = {}});
    CHECK(ScriptedLlm::last_result.contains("ville introuvable"));
    REQUIRE(transcript.error.has_value());
    CHECK(transcript.error.value_or("").contains("script épuisé"));
    CHECK(transcript.reply == "Ville introuvable.");
}

TEST_CASE("rapport Markdown", "[evals]") {
    Case ok_case;
    ok_case.id = "ok";
    ok_case.category = "simple";
    ok_case.requests = {"a"};
    Case ko_case;
    ko_case.id = "ko";
    ko_case.category = "injection";
    ko_case.requests = {"b | c"};

    Transcript transcript;
    transcript.duration = std::chrono::milliseconds{1500};
    transcript.reply = "ligne 1\nligne 2";
    transcript.calls = {{.id = "1", .name = "open_url", .arguments = {{"url", "https://x"}}}};
    const std::vector<CaseResult> results{
        {.test = &ok_case, .attempts = {{.transcript = transcript, .failures = {}}}},
        {.test = &ko_case,
         .attempts = {{.transcript = transcript, .failures = {"appel interdit : open_url"}},
                      {.transcript = transcript, .failures = {}}}},
    };
    const std::string report = make_report({.date = "2026-11-07 15:30",
                                            .model = "claude_code / sonnet",
                                            .prompt = "par défaut",
                                            .platform = "Linux"},
                                           results);
    CHECK(report.contains("**Réussite : 2/3** (66 %)"));
    CHECK(report.contains("| ✓ `ok` | simple | 1/1 | 1,5 s |"));
    CHECK(report.contains("| ~ `ko` | injection | 1/2 |"));
    CHECK(report.contains("### `ko` (passage 1)"));
    CHECK(report.contains("> b \\| c"));
    CHECK(report.contains("- réponse : « ligne 1 ligne 2 »"));
    CHECK(report.contains("`open_url {\"url\":\"https://x\"}`"));
}
