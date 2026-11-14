#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"

using namespace avenoir::core;

TEST_CASE("config.example.toml se charge", "[config]") {
    const auto config = load_config(AVENOIR_SOURCE_DIR "/config.example.toml");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 2);
    CHECK(config->models[0].name == "gemma"); // le premier du fichier sert par défaut
    CHECK(config->models[1].name == "leger");
    const auto* llm = std::get_if<OpenAiCompatibleConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->base_url == "http://localhost:11434/v1");
    CHECK(llm->temperature == 0.2);
    CHECK(config->agent.max_steps == 10);
    CHECK(config->agent.fast_path);
    CHECK(config->agent.unload_on_switch);
    CHECK_FALSE(config->agent.local_only);
    const auto local = parse_config("[agent]\nlocal_only = true\n");
    REQUIRE(local.has_value());
    CHECK(local->agent.local_only);
    CHECK(config->permissions.sensitive == Policy::Confirm);

    const auto keep = parse_config("[agent]\nunload_on_switch = false\n");
    REQUIRE(keep.has_value());
    CHECK_FALSE(keep->agent.unload_on_switch);
    CHECK_FALSE(parse_config("[agent]\nunload_on_switch = \"non\"\n").has_value());

    CHECK(config->agent.verify_replies);
    const auto trusting = parse_config("[agent]\nverify_replies = false\n");
    REQUIRE(trusting.has_value());
    CHECK_FALSE(trusting->agent.verify_replies);
    CHECK_FALSE(parse_config("[agent]\nverify_replies = \"oui\"\n").has_value());
}

TEST_CASE("valeurs par défaut et fournisseur claude_code", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "claude_code"
        model = "sonnet"
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 1);
    const auto* llm = std::get_if<ClaudeCodeConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->model == "sonnet");
    CHECK(llm->command == "claude");
    CHECK(llm->timeout_secs == 180);
    CHECK(config->agent.max_steps == 10);
    CHECK(config->modules.empty());
}

TEST_CASE("effort de réflexion d'un modèle compatible OpenAI", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "openai_compatible"
        base_url = "http://localhost:11434/v1"
        model = "qwen3.5:9b"
        reasoning_effort = "none"
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 1);
    const auto* llm = std::get_if<OpenAiCompatibleConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->reasoning_effort == "none");

    const auto invalid = parse_config(R"(
        [models.m]
        provider = "openai_compatible"
        base_url = "http://localhost:11434/v1"
        model = "m"
        reasoning_effort = "maximum"
    )");
    REQUIRE_FALSE(invalid.has_value());
    CHECK(describe(invalid.error()).contains("models.m.reasoning_effort"));
}

TEST_CASE("fournisseur anthropic", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "anthropic"
        model = "claude-sonnet-5-5"
        max_tokens = 2048
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 1);
    const auto* llm = std::get_if<AnthropicConfig>(&config->models[0].config);
    REQUIRE(llm != nullptr);
    CHECK(llm->api_key_env == "ANTHROPIC_API_KEY");
    CHECK(llm->max_tokens == 2048);
}

TEST_CASE("réglages des modules", "[config]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "claude_code"

        [modules.meteo]
        enabled = false

        [modules.calendrier]
        risk = { update_event = "sensitive", list_events = "reversible" }
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->modules.size() == 2);
    const auto& meteo = config->modules.at("meteo");
    CHECK(meteo.enabled == false);
    CHECK(meteo.risk.empty());
    const auto& calendar = config->modules.at("calendrier");
    CHECK_FALSE(calendar.enabled.has_value());
    CHECK(calendar.risk.at("update_event") == RiskLevel::Sensitive);
    CHECK(calendar.risk.at("list_events") == RiskLevel::Reversible);
}

TEST_CASE("les erreurs de configuration sont lisibles", "[config]") {
    const auto expect_error = [](const char* toml, const std::string& fragment) {
        const auto config = parse_config(toml);
        INFO(toml);
        REQUIRE_FALSE(config.has_value());
        CHECK(config.error().code == ErrorCode::Config);
        CHECK(config.error().message.find(fragment) != std::string::npos);
    };

    expect_error("[llm]\nprovider = \"claude_code\"", "remplacée par [models.<nom>]");
    expect_error("[models.m]\nprovider = \"inconnu\"", "fournisseur inconnu");
    expect_error("[models.m]\nprovider = \"anthropic\"", "models.m.model");
    expect_error("[models.m]\nprovider = \"claude_code\"\n[agent]\nmax_steps = 0",
                 "agent.max_steps");
    expect_error("[models.m]\nprovider = \"claude_code\"\n[agent]\nfast_path = \"oui\"",
                 "agent.fast_path");
    expect_error("[models.m]\nprovider = \"claude_code\"\n[permissions]\nsensitive = \"parfois\"",
                 "permissions.sensitive");
    expect_error("[llm\nprovider = 1", "TOML illisible");
    constexpr const char* kLlm = "[models.m]\nprovider = \"claude_code\"\n";
    expect_error((std::string("modules = 3\n") + kLlm).c_str(), "modules");
    expect_error((std::string(kLlm) + "[modules.meteo]\nactif = true").c_str(),
                 "modules.meteo.actif");
    expect_error((std::string(kLlm) + "[modules.meteo]\nrisk = { x = \"aucun\" }").c_str(),
                 "modules.meteo.risk.x");
    expect_error(
        (std::string(kLlm) + "[modules.meteo]\nrisk = { \"a b\" = \"sensitive\" }").c_str(),
        "nom d'outil invalide");
}

TEST_CASE("modèles dans l'ordre du fichier, ou aucun", "[config]") {
    const auto config = parse_config(R"(
        [models.zeta]
        provider = "openai_compatible"
        base_url = "http://192.168.1.15:11434/v1"
        model = "ministral-3:8b"
        local = true

        [models.alpha]
        provider = "claude_code"
        model = "haiku"
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->models.size() == 2);
    // Ordre du fichier, pas l'ordre alphabétique : « zeta » sert par défaut.
    CHECK(config->models[0].name == "zeta");
    CHECK(config->models[0].local == true);
    CHECK(config->models[1].name == "alpha");
    CHECK(std::holds_alternative<ClaudeCodeConfig>(config->models[1].config));

    // Aucun modèle : configuration valide, seule la voie rapide répondra.
    const auto empty = parse_config("[agent]\nmax_steps = 5\n");
    REQUIRE(empty.has_value());
    CHECK(empty->models.empty());
    CHECK(parse_config("").has_value());
}

TEST_CASE("modèles : erreurs lisibles", "[config]") {
    const auto error_of = [](const std::string& text) {
        const auto config = parse_config(text);
        REQUIRE_FALSE(config.has_value());
        return config.error().message;
    };
    CHECK(error_of("[models.Gros]\nprovider = \"claude_code\"\n").contains("models.Gros"));
    CHECK(error_of("[models.\"../x\"]\nprovider = \"claude_code\"\n").contains("nom invalide"));
    CHECK(error_of("[models.x]\nprovider = \"openai_compatible\"\nmodel = \"m\"\n")
              .contains("models.x.base_url"));
    CHECK(error_of("[models.x]\nprovider = \"claude_code\"\nlocal = true\n")
              .contains("models.x.local"));
    CHECK(error_of("models = 3\n").contains("models"));
}

TEST_CASE("format_config est relu à l'identique", "[config]") {
    AppConfig config;
    config.models.push_back(
        NamedLlm{.name = "zeta", // pas le premier par ordre alphabétique : l'ordre est gardé
                 .config = OpenAiCompatibleConfig{.base_url = "http://192.168.1.20:11434/v1",
                                                  .model = "gemma4:26b-a4b",
                                                  .api_key_env = std::nullopt,
                                                  .temperature = 1.0,
                                                  .reasoning_effort = "none"},
                 .local = true});
    config.models.push_back(NamedLlm{.name = "claude",
                                     .config = ClaudeCodeConfig{.model = "haiku",
                                                                .command = "claude",
                                                                .timeout_secs = 90,
                                                                .persistent = false},
                                     .local = std::nullopt});
    config.models.push_back(NamedLlm{.name = "sonnet",
                                     .config = AnthropicConfig{.model = "claude-sonnet-5-5",
                                                               .api_key_env = "MA_CLE",
                                                               .max_tokens = 2048,
                                                               .base_url = std::nullopt},
                                     .local = std::nullopt});
    config.agent.local_only = true;
    config.agent.max_steps = 4;
    config.agent.system_prompt = "Ligne 1\n« guillemets \" » et \\ barre\t\x01";
    config.permissions.reversible = Policy::Confirm;
    config.web.engine = "duckduckgo";
    config.web.searxng_idle_minutes = 3;
    config.modules["meteo"].enabled = false;
    config.modules["autre.module"].risk.emplace("lire", RiskLevel::Sensitive);
    config.journal.retention_days = 30;
    config.shortcut.enabled = true;
    config.gpu = GpuConfig{.mode = "switch", .light_model = "claude", .threshold_gb = 4};
    config.modules["calendrier"].enabled = true;
    config.modules["calendrier"].granted = {"network", "files.read"};
    config.permissions.tools.emplace(
        "meteo__get_forecast",
        ToolRule{.policy = Policy::Allow, .risk = RiskLevel::Reversible, .version = "1.2.0"});
    config.permissions.tools.emplace(
        "power", ToolRule{.policy = Policy::Deny, .risk = RiskLevel::Sensitive, .version = {}});

    const std::string text = format_config(config);
    const auto parsed = parse_config(text);
    INFO(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == config);
}

TEST_CASE("format_config n'écrit que ce qui diffère des valeurs par défaut", "[config]") {
    const std::string text = format_config(AppConfig{});
    CHECK(text.find('[') == std::string::npos);
    const auto parsed = parse_config(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == AppConfig{});
}

TEST_CASE("save_config écrit un fichier relu à l'identique", "[config]") {
    namespace fs = std::filesystem;
    const fs::path directory =
        fs::temp_directory_path() /
        ("avenoir-config-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path path = directory / "sous" / "config.toml";
    AppConfig config;
    config.agent.fast_path = false;
    REQUIRE(save_config(path, config).has_value());
    const auto loaded = load_config(path);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == config);
    CHECK_FALSE(fs::exists(fs::path(path.string() + ".tmp")));
#ifndef _WIN32
    const auto perms = fs::status(path).permissions();
    CHECK((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif
    // Remplacement d'un fichier existant.
    config.agent.fast_path = true;
    REQUIRE(save_config(path, config).has_value());
    CHECK(load_config(path).value() == config);
    std::error_code ignored;
    fs::remove_all(directory, ignored);
}

TEST_CASE("default_config_path : dossier de configuration de l'utilisateur", "[config]") {
    const auto path = default_config_path().value_or(std::filesystem::path{});
    REQUIRE_FALSE(path.empty()); // HOME ou APPDATA existent en test
    CHECK(path.filename() == "config.toml");
    CHECK(path.parent_path().filename() == "avenoir");
    CHECK(path.is_absolute());
}

TEST_CASE("[agents.<nom>] et [orchestration] : lus dans l'ordre, relus à l'identique", "[config]") {
    const auto config = parse_config(R"(
[models.gemma]
provider = "openai_compatible"
base_url = "http://localhost:11434/v1"
model = "gemma4:26b-a4b"

[orchestration]
mode = "configured"
max_parallel = 2

[agents.zeta-dev]
role = "développeur"
instructions = "Code en C++23."
model = "gemma"
tools = ["web_search", "read_webpage", "web_search"]
max_steps = 12

[agents.alpha]
role = "relecteur"
)");
    REQUIRE(config.has_value());
    REQUIRE(config->agents.size() == 2);
    CHECK(config->agents[0].name == "zeta-dev"); // ordre du fichier
    CHECK(config->agents[0].tools == std::vector<std::string>{"web_search", "read_webpage"});
    CHECK(config->agents[0].model == "gemma");
    CHECK(config->agents[0].max_steps == 12);
    CHECK(config->agents[1].tools.empty());
    CHECK(config->orchestration.mode == "configured");
    CHECK(config->orchestration.max_parallel == 2);
    CHECK(config->orchestration.max_tasks == 12);

    const auto reread = parse_config(format_config(*config));
    REQUIRE(reread.has_value());
    CHECK(*reread == *config);
}

TEST_CASE("[agents.<nom>] : refus", "[config]") {
    CHECK_FALSE(parse_config("[agents.a]\ninstructions = \"x\"\n").has_value()); // rôle manquant
    CHECK_FALSE(parse_config("[agents.Grand]\nrole = \"x\"\n").has_value());     // nom invalide
    CHECK_FALSE(parse_config("[agents.a]\nrole = \"x\"\nmodel = \"absent\"\n").has_value());
    CHECK_FALSE(parse_config("[agents.a]\nrole = \"x\"\ntools = \"web_search\"\n").has_value());
    CHECK_FALSE(parse_config("[agents.a]\nrole = \"x\"\ntools = [\"a b\"]\n").has_value());
    CHECK_FALSE(parse_config("[agents.a]\nrole = \"x\"\ninconnue = 1\n").has_value());
    CHECK_FALSE(parse_config("[agents.a]\nrole = \"x\"\nmax_steps = 0\n").has_value());
    CHECK_FALSE(parse_config("[orchestration]\nmode = \"libre\"\n").has_value());
    CHECK_FALSE(parse_config("[orchestration]\nmax_parallel = 99\n").has_value());
}

TEST_CASE("[permissions.tools] et granted : lus, refus lisibles", "[config][rules]") {
    const auto config = parse_config(R"(
        [models.m]
        provider = "claude_code"

        [permissions.tools]
        lock_screen = { policy = "allow", risk = "reversible" }
        power = { policy = "deny" }

        [modules.calendrier]
        enabled = true
        granted = ["network"]
    )");
    REQUIRE(config.has_value());
    REQUIRE(config->permissions.tools.size() == 2);
    const auto& lock = config->permissions.tools.at("lock_screen");
    CHECK(lock.policy == Policy::Allow);
    CHECK(lock.risk == RiskLevel::Reversible);
    CHECK_FALSE(lock.version.has_value());
    CHECK(config->permissions.tools.at("power").policy == Policy::Deny);
    CHECK(config->modules.at("calendrier").granted == std::vector<std::string>{"network"});

    const auto expect_error = [](const std::string& body, const std::string& fragment) {
        const std::string toml = "[models.m]\nprovider = \"claude_code\"\n" + body;
        const auto parsed = parse_config(toml);
        INFO(toml);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().message.find(fragment) != std::string::npos);
    };
    // « confirmer » n'est pas une règle durable ; « autoriser » exige le risque accordé.
    expect_error("[permissions.tools]\nx = { policy = \"confirm\" }", "allow ou deny");
    expect_error("[permissions.tools]\nx = { policy = \"allow\" }", "permissions.tools.x.risk");
    expect_error("[permissions.tools]\nx = { policy = \"allow\", risk = \"rien\" }",
                 "permissions.tools.x.risk");
    expect_error("[permissions.tools]\n\"a;b\" = { policy = \"deny\" }", "nom d'outil invalide");
    expect_error("[permissions.tools]\nx = { policy = \"deny\", toujours = true }", "clé inconnue");
    expect_error("[permissions.tools]\nx = \"allow\"", "table attendue");
    expect_error("[permissions]\ntools = 3", "permissions.tools");
    expect_error("[modules.a]\ngranted = \"network\"", "modules.a.granted");
    expect_error("[modules.a]\ngranted = [\"rés eau\"]", "permission invalide");
}

TEST_CASE("[journal] : durée de conservation", "[config][journal]") {
    const auto config = parse_config("[journal]\nretention_days = 7\n");
    REQUIRE(config.has_value());
    CHECK(config->journal.retention_days == 7);
    CHECK(parse_config("").value_or(AppConfig{}).journal.retention_days == 90);
    CHECK_FALSE(parse_config("[journal]\nretention_days = 0\n").has_value());
    CHECK_FALSE(parse_config("[journal]\nretention_days = 4000\n").has_value());
}

TEST_CASE("[gpu] : mode, modèle léger, seuil", "[config][gpu]") {
    constexpr const char* kModels = "[models.gros]\nprovider = \"claude_code\"\n"
                                    "[models.leger]\nprovider = \"claude_code\"\n";
    const auto config =
        parse_config(std::string(kModels) + "[gpu]\nmode = \"switch\"\nlight_model = \"leger\"\n");
    REQUIRE(config.has_value());
    CHECK(config->gpu.mode == "switch");
    CHECK(config->gpu.light_model == "leger");
    CHECK(config->gpu.threshold_gb == 2);
    CHECK(parse_config("").value_or(AppConfig{}).gpu.mode == "off");
    const auto refused = [](const std::string& gpu) {
        return !parse_config(std::string(kModels) + "[gpu]\n" + gpu).has_value();
    };
    CHECK(refused("mode = \"parfois\""));
    CHECK(refused("mode = \"switch\"")); // sans modèle léger
    CHECK(refused("light_model = \"inconnu\""));
    CHECK(refused("threshold_gb = 0"));
    CHECK_FALSE(refused("mode = \"unload\"\nthreshold_gb = 6"));
}

TEST_CASE("[shortcut] : raccourci global", "[config]") {
    CHECK_FALSE(parse_config("").value_or(AppConfig{}).shortcut.enabled);
    CHECK(parse_config("[shortcut]\nenabled = true\n").value_or(AppConfig{}).shortcut.enabled);
    CHECK_FALSE(parse_config("[shortcut]\nenabled = \"oui\"\n").has_value());
}
