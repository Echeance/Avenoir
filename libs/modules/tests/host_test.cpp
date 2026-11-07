// Tests de l'hôte avec un vrai processus : le module de test (fixture_module).

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/router.hpp"
#include "avenoir/modules/discovery.hpp"
#include "avenoir/modules/host.hpp"

using namespace avenoir;
using namespace avenoir::modules;
using namespace std::chrono_literals;

namespace {

/// Dossier temporaire supprimé à la fin du test.
class TempDir {
  public:
    TempDir() {
        std::random_device random;
        path_ =
            std::filesystem::temp_directory_path() / ("avenoir-host-" + std::to_string(random()));
        std::filesystem::create_directories(path_);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

  private:
    std::filesystem::path path_;
};

constexpr const char* kTools = R"(
[[tools]]
name = "echo"
risk = "read_only"

[[tools]]
name = "slow"
risk = "read_only"

[[tools]]
name = "crash"

[[tools]]
name = "fail"
risk = "read_only"

[[tools]]
name = "set_value"
risk = "reversible"

[[tools]]
name = "env"
risk = "read_only"

[[tools]]
name = "noisy"
risk = "read_only"

[[tools]]
name = "host_call"
risk = "read_only"

[[tools]]
name = "bad_schema"

[[tools]]
name = "absent"

[[commands]]
name = "echo"
tool = "echo"
arg = "text"

[[commands]]
name = "aide"
tool = "echo"

[[fast_path]]
pattern = '^répète (.+)$'
tool = "echo"
arg = "text"
)";

/// Installe le module de test dans `<root>/<id>` avec le manifeste donné.
void install(const std::filesystem::path& root, const std::string& id, const std::string& extra,
             const std::string& run_args = "", const std::string& tools = kTools) {
    const auto directory = root / id;
    std::filesystem::create_directories(directory);
    const std::filesystem::path fixture(AVENOIR_FIXTURE_MODULE);
    std::filesystem::copy_file(fixture, directory / ("binaire" + fixture.extension().string()),
                               std::filesystem::copy_options::overwrite_existing);
    std::ofstream(directory / "module.toml")
        << "id = \"" << id << "\"\nname = \"Test " << id << "\"\nversion = \"1.0\"\n"
        << "run = [\"binaire\"" << run_args << "]\n"
        << "env = [\"TEST_MODULE_PUBLIC\"]\n"
        << extra << "\n"
        << tools;
}

struct Loaded {
    ModuleHost host;
    core::ToolRegistry tools;
    std::vector<std::string> warnings;
};

Loaded load(const std::filesystem::path& root, Origin origin = Origin::Bundled,
            HostOptions options = {},
            const std::map<std::string, core::ModuleSettings, std::less<>>& settings = {},
            const std::vector<RiskOverride>& overrides = {}) {
    const std::vector<SearchDirectory> directories{{.path = root, .origin = origin}};
    const Discovery discovery = discover(directories);
    for (const auto& problem : discovery.problems) {
        INFO(problem.message);
    }
    REQUIRE(discovery.problems.empty());
    Loaded loaded{.host = ModuleHost(std::move(options)), .tools = {}, .warnings = {}};
    loaded.host.load(discovery, settings, overrides);
    loaded.host.register_tools(loaded.tools);
    loaded.warnings = loaded.host.take_warnings();
    return loaded;
}

core::ToolResult call(const core::ToolRegistry& tools, const std::string& name,
                      const nlohmann::json& arguments = nlohmann::json::object(),
                      const std::stop_token& stop = {}) {
    const auto tool = tools.find(name);
    REQUIRE(tool != nullptr);
    return tool->call(arguments, stop);
}

bool has_warning(const std::vector<std::string>& warnings, const std::string& fragment) {
    return std::ranges::any_of(warnings,
                               [&](const std::string& w) { return w.contains(fragment); });
}

void set_variable(const char* name, const char* value) {
#ifdef _WIN32
    REQUIRE(_putenv_s(name, value) == 0);
#else
    REQUIRE(setenv(name, value, 1) == 0); // NOLINT(concurrency-mt-unsafe)
#endif
}

} // namespace

TEST_CASE("un module est lancé et ses outils déclarés sont exposés", "[host]") {
    const TempDir temp;
    install(temp.path(), "essai", "");
    const auto loaded = load(temp.path());

    const auto statuses = loaded.host.statuses();
    REQUIRE(statuses.size() == 1);
    INFO(statuses[0].message);
    REQUIRE(statuses[0].state == ModuleState::Running);

    // Déclarés et fournis : exposés avec le préfixe du module.
    CHECK(loaded.tools.find("essai__echo") != nullptr);
    CHECK(loaded.tools.find("essai__set_value") != nullptr);
    // Fourni mais absent du manifeste, schéma invalide, déclaré mais non fourni.
    CHECK(loaded.tools.find("essai__undeclared") == nullptr);
    CHECK(loaded.tools.find("essai__bad_schema") == nullptr);
    CHECK(loaded.tools.find("essai__absent") == nullptr);
    CHECK(has_warning(loaded.warnings, "undeclared"));
    CHECK(has_warning(loaded.warnings, "bad_schema"));
    CHECK(has_warning(loaded.warnings, "absent"));

    const auto spec = loaded.tools.find("essai__echo")->spec();
    CHECK(spec.risk == core::RiskLevel::ReadOnly);
    CHECK(spec.description.starts_with("[Module « Test essai »]"));
    CHECK(loaded.tools.find("essai__crash")->spec().risk == core::RiskLevel::Sensitive);

    const auto echoed = call(loaded.tools, "essai__echo", {{"text", "bonjour"}});
    REQUIRE(echoed.has_value());
    CHECK(echoed->content == "écho : bonjour");
    CHECK_FALSE(echoed->undo.has_value());

    const auto failed = call(loaded.tools, "essai__fail");
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().message == "échec voulu");
}

TEST_CASE("annulation d'une action par avenoir/undo", "[host]") {
    const TempDir temp;
    install(temp.path(), "essai", "");
    const auto loaded = load(temp.path());

    const auto changed = call(loaded.tools, "essai__set_value", {{"value", "nouvelle"}});
    REQUIRE(changed.has_value());
    const std::optional<core::UndoAction>& undo = changed->undo;
    REQUIRE(undo.has_value());
    if (!undo) {
        return; // Pour l'analyse statique (REQUIRE a déjà échoué).
    }
    CHECK(undo->description == "remettre la valeur « initiale »");
    const auto undone = undo->run();
    REQUIRE(undone.has_value());
    CHECK(undone->content == "valeur restaurée : initiale");
    CHECK_FALSE(undone->undo.has_value());
}

TEST_CASE("le module ne voit qu'un environnement minimal", "[host]") {
    set_variable("TEST_MODULE_SECRET", "secret");
    set_variable("TEST_MODULE_PUBLIC", "public");
    const TempDir temp;
    install(temp.path(), "essai", "");
    HostOptions options;
    options.data_root = temp.path() / "donnees";
    auto loaded = load(temp.path(), Origin::Bundled, options);

    const auto read = [&](const char* name) {
        const auto result = call(loaded.tools, "essai__env", {{"name", name}});
        REQUIRE(result.has_value());
        return result->content;
    };
    CHECK(read("TEST_MODULE_SECRET") == "(absente)");
    CHECK(read("TEST_MODULE_PUBLIC") == "public"); // déclarée dans le manifeste
    CHECK(read("AVENOIR_MODULE_ID") == "essai");
    CHECK(read("AVENOIR_MODULE_DATA") == (temp.path() / "donnees" / "essai").string());
    CHECK(std::filesystem::is_directory(temp.path() / "donnees" / "essai"));
    CHECK(read("PATH") != "(absente)");
}

TEST_CASE("les sorties piégées d'un module sont neutralisées", "[host]") {
    const TempDir temp;
    install(temp.path(), "essai", "");
    HostOptions options;
    options.log_root = temp.path() / "logs";
    auto loaded = load(temp.path(), Origin::Bundled, options);

    const auto noisy = call(loaded.tools, "essai__noisy");
    REQUIRE(noisy.has_value());
    // Ni séquence d'échappement, ni sonnerie, ni inversion bidirectionnelle.
    CHECK(noisy->content == "[2Jécran effacé et  inversé");

    // Requête du module vers l'hôte : refusée proprement.
    const auto host_call = call(loaded.tools, "essai__host_call");
    REQUIRE(host_call.has_value());
    CHECK(host_call->content == "refusé : méthode non prise en charge par Avenoir");
    // Le module fonctionne toujours.
    CHECK(call(loaded.tools, "essai__echo", {{"text", "ok"}}).has_value());

    // Arrête le module : le log est alors complet.
    loaded.tools = core::ToolRegistry{};
    {
        const ModuleHost stopped(std::move(loaded.host));
    }
    std::ifstream log(temp.path() / "logs" / "essai.log");
    const std::string content((std::istreambuf_iterator<char>(log)), {});
    CHECK(content.contains("module de test prêt"));
    CHECK(content.contains("?[31mrouge?[0m sur stderr"));
    CHECK(content.contains("message illisible ignoré"));
}

TEST_CASE("un module qui plante ou traîne ne bloque pas l'assistant", "[host]") {
    const TempDir temp;
    install(temp.path(), "fragile", "");
    install(temp.path(), "lent", "");
    HostOptions options;
    options.call_timeout = 300ms;
    const auto loaded = load(temp.path(), Origin::Bundled, options);

    const auto slow = call(loaded.tools, "lent__slow", {{"ms", 2000}});
    REQUIRE_FALSE(slow.has_value());
    CHECK(slow.error().message.contains("pas de réponse du module en 300 ms"));

    // Ctrl+C pendant un appel : rend la main tout de suite.
    std::stop_source source;
    const std::jthread canceller([&source] {
        std::this_thread::sleep_for(100ms);
        source.request_stop();
    });
    const auto started = std::chrono::steady_clock::now();
    const auto cancelled = call(loaded.tools, "lent__slow", {{"ms", 5000}}, source.get_token());
    CHECK_FALSE(cancelled.has_value());
    CHECK(std::chrono::steady_clock::now() - started < 1s);

    const auto crashed = call(loaded.tools, "fragile__crash");
    REQUIRE_FALSE(crashed.has_value());
    CHECK(crashed.error().message.contains("arrêté"));
    // Les appels suivants échouent proprement, et l'état le signale.
    const auto after = call(loaded.tools, "fragile__echo", {{"text", "x"}});
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().message.contains("arrêté"));
    const auto statuses = loaded.host.statuses();
    CHECK(statuses[0].state == ModuleState::Failed);
    CHECK(statuses[1].state == ModuleState::Running); // l'autre module n'est pas touché
}

TEST_CASE("échecs au lancement : signalés sans empêcher les autres modules", "[host]") {
    const TempDir temp;
    install(temp.path(), "a-sort", "", ", \"--exit-at-start\"");
    install(temp.path(), "b-muet", "", ", \"--no-initialize\"");
    install(temp.path(), "c-bon", "");
    HostOptions options;
    options.handshake_timeout = 300ms;
    const auto loaded = load(temp.path(), Origin::Bundled, options);

    const auto statuses = loaded.host.statuses();
    REQUIRE(statuses.size() == 3);
    CHECK(statuses[0].state == ModuleState::Failed);
    CHECK(statuses[1].state == ModuleState::Failed);
    CHECK(statuses[1].message.contains("pas de réponse"));
    CHECK(statuses[2].state == ModuleState::Running);
    CHECK(loaded.tools.find("c-bon__echo") != nullptr);
}

TEST_CASE("un module utilisateur reste désactivé tant qu'il n'est pas activé", "[host]") {
    const TempDir temp;
    install(temp.path(), "essai", "");
    {
        const auto loaded = load(temp.path(), Origin::User);
        CHECK(loaded.host.statuses()[0].state == ModuleState::Disabled);
        CHECK(loaded.tools.empty());
    }
    std::map<std::string, core::ModuleSettings, std::less<>> settings;
    settings["essai"].enabled = true;
    settings["inconnu"].enabled = true;
    const auto loaded = load(temp.path(), Origin::User, {}, settings);
    CHECK(loaded.host.statuses()[0].state == ModuleState::Running);
    CHECK(has_warning(loaded.warnings, "inconnu"));
}

TEST_CASE("serveur MCP : outils sensibles, pas d'annulation, pagination", "[host]") {
    const TempDir temp;
    install(temp.path(), "serveur", R"(protocol = "mcp")", R"(, "--mcp", "--paginate")", "");
    const auto loaded = load(temp.path());

    // Sans [[tools]], tous les outils valides sont exposés, en `sensitive`.
    const auto echo = loaded.tools.find("serveur__echo"); // première page
    const auto value = loaded.tools.find("serveur__set_value");
    REQUIRE(echo != nullptr);
    REQUIRE(value != nullptr);
    CHECK(loaded.tools.find("serveur__undeclared") != nullptr);
    CHECK(loaded.tools.find("serveur__bad_schema") == nullptr);
    CHECK(echo->spec().risk == core::RiskLevel::Sensitive);

    const auto changed = call(loaded.tools, "serveur__set_value", {{"value", "x"}});
    REQUIRE(changed.has_value());
    CHECK_FALSE(changed->undo.has_value());
}

TEST_CASE("commandes et voie rapide des modules", "[host]") {
    const TempDir temp;
    install(temp.path(), "essai", "");
    auto loaded = load(temp.path());

    const std::vector<std::string> reserved{"aide", "quitter"};
    const auto commands = loaded.host.commands(reserved);
    REQUIRE(commands.size() == 1); // « /aide » est refusée
    CHECK(commands[0].name == "echo");
    CHECK(commands[0].tool == "essai__echo");
    CHECK(commands[0].arg == "text");
    CHECK(has_warning(loaded.host.take_warnings(), "/aide"));

    auto router = core::FastRouter::with_defaults();
    REQUIRE(router.has_value());
    loaded.host.register_fast_path(*router);
    CHECK(router->route("Répète bonjour") ==
          core::FastRoute{.tool = "essai__echo", .arguments = {{"text", "bonjour"}}});
    // Les règles d'Avenoir restent prioritaires.
    CHECK(router->route("quelle heure est-il ?") ==
          core::FastRoute{.tool = "current_time", .arguments = nlohmann::json::object()});
}

TEST_CASE("risque effectif d'un outil de module", "[host]") {
    using core::RiskLevel;
    const auto resolve = [](std::optional<RiskLevel> declared, std::optional<RiskLevel> config,
                            std::optional<RiskOverride> chosen) {
        core::ModuleSettings settings;
        if (config) {
            settings.risk["outil"] = *config;
        }
        std::vector<RiskOverride> overrides;
        if (chosen) {
            overrides.push_back(*chosen);
        }
        return resolve_risk("m__outil", "outil", declared, &settings, overrides);
    };
    const auto chosen = [](RiskLevel risk, bool agreed) {
        return RiskOverride{.tool = "m__outil", .risk = risk, .agreed = agreed};
    };

    // Manifeste muet : sensible.
    CHECK(resolve(std::nullopt, std::nullopt, std::nullopt).risk == RiskLevel::Sensitive);
    // config.toml relève, mais n'abaisse pas.
    CHECK(resolve(RiskLevel::ReadOnly, RiskLevel::Reversible, std::nullopt).risk ==
          RiskLevel::Reversible);
    const auto lowered = resolve(RiskLevel::Sensitive, RiskLevel::ReadOnly, std::nullopt);
    CHECK(lowered.risk == RiskLevel::Sensitive);
    CHECK(lowered.warning.contains("config.toml"));
    // /risque : relever est libre, abaisser exige l'accord.
    CHECK(resolve(RiskLevel::ReadOnly, std::nullopt, chosen(RiskLevel::Sensitive, false)).risk ==
          RiskLevel::Sensitive);
    const auto refused =
        resolve(RiskLevel::Sensitive, std::nullopt, chosen(RiskLevel::ReadOnly, false));
    CHECK(refused.risk == RiskLevel::Sensitive);
    CHECK(refused.warning.contains("sans accord"));
    CHECK(resolve(RiskLevel::Sensitive, std::nullopt, chosen(RiskLevel::ReadOnly, true)).risk ==
          RiskLevel::ReadOnly);
    // Accord + config.toml plus strict : le plus strict l'emporte.
    CHECK(resolve(RiskLevel::Sensitive, RiskLevel::Reversible, chosen(RiskLevel::ReadOnly, true))
              .risk == RiskLevel::Reversible);
    CHECK(resolve_risk("m__outil", "outil", RiskLevel::ReadOnly, nullptr, {}).risk ==
          RiskLevel::ReadOnly);
}

TEST_CASE("un programme qui sort du dossier du module est refusé", "[host]") {
#ifndef _WIN32
    const TempDir temp;
    install(temp.path(), "essai", "");
    // Lien symbolique vers un programme hors du dossier.
    const auto directory = temp.path() / "essai";
    std::filesystem::remove(directory / "binaire");
    std::filesystem::create_symlink(AVENOIR_FIXTURE_MODULE, directory / "binaire");
    const auto loaded = load(temp.path());
    const auto statuses = loaded.host.statuses();
    CHECK(statuses[0].state == ModuleState::Failed);
    CHECK(statuses[0].message.contains("hors du dossier"));
#endif
}

TEST_CASE("changement de risque à chaud (/risque)", "[host]") {
    const TempDir temp;
    install(temp.path(), "essai", "");
    std::map<std::string, core::ModuleSettings, std::less<>> settings;
    settings["essai"].risk["echo"] = core::RiskLevel::Reversible; // relevé dans config.toml
    auto loaded = load(temp.path(), Origin::Bundled, {}, settings);
    const auto echo = loaded.tools.find("essai__echo");
    const auto value = loaded.tools.find("essai__set_value");
    REQUIRE(echo != nullptr);
    REQUIRE(value != nullptr);
    CHECK(echo->spec().risk == core::RiskLevel::Reversible);

    // Relever : immédiat, sans accord.
    auto decision = loaded.host.choose_risk(
        {.tool = "essai__set_value", .risk = core::RiskLevel::Sensitive, .agreed = false});
    REQUIRE(decision.has_value());
    CHECK(decision->risk == core::RiskLevel::Sensitive);
    CHECK(value->spec().risk == core::RiskLevel::Sensitive);

    // Abaisser sans accord : refusé.
    decision = loaded.host.choose_risk(
        {.tool = "essai__set_value", .risk = core::RiskLevel::ReadOnly, .agreed = false});
    REQUIRE(decision.has_value());
    CHECK(decision->risk == core::RiskLevel::Reversible);
    CHECK_FALSE(decision->warning.empty());

    // Abaisser avec accord.
    decision = loaded.host.choose_risk(
        {.tool = "essai__set_value", .risk = core::RiskLevel::ReadOnly, .agreed = true});
    REQUIRE(decision.has_value());
    CHECK(value->spec().risk == core::RiskLevel::ReadOnly);
    const auto info = loaded.host.tool_info("essai__set_value");
    REQUIRE(info.has_value());
    if (!info) {
        return; // Pour l'analyse statique (REQUIRE a déjà échoué).
    }
    CHECK(info->risk == core::RiskLevel::ReadOnly);

    // config.toml reste un plancher, même avec accord.
    decision = loaded.host.choose_risk(
        {.tool = "essai__echo", .risk = core::RiskLevel::ReadOnly, .agreed = true});
    REQUIRE(decision.has_value());
    CHECK(echo->spec().risk == core::RiskLevel::Reversible);

    // Retour au manifeste.
    REQUIRE(loaded.host.clear_risk("essai__set_value").has_value());
    CHECK(value->spec().risk == core::RiskLevel::Reversible);

    CHECK_FALSE(
        loaded.host
            .choose_risk({.tool = "launch_app", .risk = core::RiskLevel::ReadOnly, .agreed = true})
            .has_value());
}
