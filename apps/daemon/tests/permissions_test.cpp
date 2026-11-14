#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/permission.hpp"
#include "avenoir/core/tool.hpp"

#include "permissions.hpp"

using namespace avenoir;
using core::Policy;
using core::RiskLevel;
using core::ToolRule;

namespace {

core::ToolSpec spec(std::string name, RiskLevel risk) {
    return core::ToolSpec{.name = std::move(name),
                          .description = "",
                          .input_schema = nlohmann::json::object(),
                          .risk = risk};
}

} // namespace

TEST_CASE("active_rules : un accord vaut pour la version du module", "[permissions]") {
    const core::ToolRuleMap written{
        {"meteo__forecast",
         ToolRule{.policy = Policy::Allow, .risk = RiskLevel::Reversible, .version = "1.0"}},
        {"meteo__alerts",
         ToolRule{.policy = Policy::Deny, .risk = RiskLevel::Sensitive, .version = "1.0"}},
        {"lock_screen",
         ToolRule{.policy = Policy::Allow, .risk = RiskLevel::Reversible, .version = {}}},
    };
    std::vector<std::string> warnings;
    SECTION("même version : tout en vigueur") {
        const auto active = daemon::active_rules(
            written, {{"meteo__forecast", "1.0"}, {"meteo__alerts", "1.0"}}, warnings);
        CHECK(active.size() == 3);
        CHECK(warnings.empty());
    }
    SECTION("module mis à jour : l'accord est écarté, le refus reste") {
        const auto active = daemon::active_rules(
            written, {{"meteo__forecast", "2.0"}, {"meteo__alerts", "2.0"}}, warnings);
        CHECK_FALSE(active.contains("meteo__forecast"));
        CHECK(active.contains("meteo__alerts"));
        CHECK(active.contains("lock_screen"));
        REQUIRE(warnings.size() == 1);
        CHECK(warnings[0].contains("meteo__forecast"));
    }
    SECTION("accord sans version pour un outil de module : écarté") {
        const core::ToolRuleMap unversioned{
            {"meteo__forecast",
             ToolRule{.policy = Policy::Allow, .risk = RiskLevel::Reversible, .version = {}}}};
        CHECK(daemon::active_rules(unversioned, {{"meteo__forecast", "1.0"}}, warnings).empty());
    }
}

TEST_CASE("make_rule : risque actuel et version du module", "[permissions]") {
    const daemon::ToolVersions versions{{"meteo__forecast", "1.4"}};
    const auto module_rule =
        daemon::make_rule(Policy::Allow, spec("meteo__forecast", RiskLevel::Reversible), versions);
    CHECK(module_rule.policy == Policy::Allow);
    CHECK(module_rule.risk == RiskLevel::Reversible);
    CHECK(module_rule.version == "1.4");
    const auto native =
        daemon::make_rule(Policy::Deny, spec("power", RiskLevel::Sensitive), versions);
    CHECK(native.policy == Policy::Deny);
    CHECK_FALSE(native.version.has_value());
}

TEST_CASE("revoke_rules : une règle, toutes, ou erreur lisible", "[permissions]") {
    core::AppConfig config;
    config.permissions.tools.emplace(
        "power", ToolRule{.policy = Policy::Deny, .risk = RiskLevel::Sensitive, .version = {}});
    config.permissions.tools.emplace(
        "lock_screen",
        ToolRule{.policy = Policy::Allow, .risk = RiskLevel::Reversible, .version = {}});

    const auto one = daemon::revoke_rules(config, "power");
    REQUIRE(one.has_value());
    CHECK(one->tools == std::vector<std::string>{"power"});
    CHECK(one->updated.permissions.tools.size() == 1);

    const auto all = daemon::revoke_rules(config, "tout");
    REQUIRE(all.has_value());
    CHECK(all->tools.size() == 2);
    CHECK(all->updated.permissions.tools.empty());

    const auto unknown = daemon::revoke_rules(config, "open_url");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message.contains("open_url"));
    CHECK_FALSE(daemon::revoke_rules(core::AppConfig{}, "tout").has_value());
}

TEST_CASE("PermissionKeeper : enregistré, en vigueur aussitôt, révocable", "[permissions]") {
    core::AppConfig config;
    std::vector<core::AppConfig> saved;
    bool fail = false;
    daemon::PermissionKeeper keeper(
        config,
        [&saved, &fail](const core::AppConfig& updated) -> core::Result<void> {
            if (fail) {
                return core::make_error(core::ErrorCode::Config, "disque plein");
            }
            saved.push_back(updated);
            return {};
        },
        {});
    const auto rules = keeper.rules();

    REQUIRE(keeper.remember(spec("lock_screen", RiskLevel::Reversible), Policy::Allow));
    REQUIRE(saved.size() == 1);
    CHECK(saved[0].permissions.tools.contains("lock_screen"));
    CHECK(config.permissions.tools.contains("lock_screen")); // configuration du démon à jour
    REQUIRE(rules->find("lock_screen").has_value());         // en vigueur pour tous les agents

    // Échec d'écriture : rien n'est changé.
    fail = true;
    CHECK_FALSE(keeper.remember(spec("power", RiskLevel::Sensitive), Policy::Deny));
    CHECK_FALSE(rules->find("power").has_value());
    CHECK_FALSE(config.permissions.tools.contains("power"));
    fail = false;

    const auto revoked = keeper.revoke("lock_screen");
    REQUIRE(revoked.has_value());
    CHECK_FALSE(rules->find("lock_screen").has_value());
    CHECK(config.permissions.tools.empty());
}

TEST_CASE("PermissionKeeper : alerte au démarrage pour une action sensible", "[permissions]") {
    core::AppConfig config;
    config.permissions.tools.emplace(
        "power", ToolRule{.policy = Policy::Allow, .risk = RiskLevel::Sensitive, .version = {}});
    daemon::PermissionKeeper keeper(
        config, [](const core::AppConfig&) -> core::Result<void> { return {}; }, {});
    const auto warnings = keeper.take_warnings();
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].contains("power"));
    CHECK(keeper.take_warnings().empty());
}
