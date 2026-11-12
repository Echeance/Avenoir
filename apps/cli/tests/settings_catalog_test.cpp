#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/permission.hpp"

#include "settings_catalog.hpp"

using namespace avenoir;
using cli::KnownModule;

namespace {

/// Un module fourni avec Avenoir, un installé par l'utilisateur.
std::vector<KnownModule> known() {
    return {{.id = "meteo", .bundled = true}, {.id = "communaute", .bundled = false}};
}

} // namespace

TEST_CASE("list_settings : valeurs actuelles en français", "[reglages]") {
    core::AppConfig config;
    config.agent.local_only = true;
    const auto lines = cli::list_settings(config, known());
    const auto find = [&lines](const std::string& key) {
        return *std::ranges::find(lines, key, &cli::SettingLine::key);
    };
    CHECK(find("agent.local_only").value == "oui");
    CHECK(find("agent.local_only").security);
    CHECK(find("permissions.sensitive").value == "confirmer");
    CHECK(find("web.searxng_url").value == "aucune");
    CHECK(find("modules.meteo.enabled").value == "oui");      // fourni : activé par défaut
    CHECK(find("modules.communaute.enabled").value == "non"); // utilisateur : désactivé
    CHECK(find("modules.communaute.enabled").security);
}

TEST_CASE("change_setting : renforcer sans accord, affaiblir avec", "[reglages]") {
    const core::AppConfig config;

    auto stronger = cli::change_setting(config, "agent.local_only", "oui", known());
    REQUIRE(stronger.has_value());
    CHECK(stronger->updated.agent.local_only);
    CHECK_FALSE(stronger->weakening.has_value());
    CHECK(stronger->strengthens);
    CHECK(stronger->before == "non");
    CHECK(stronger->after == "oui");

    auto weaker = cli::change_setting(stronger->updated, "agent.local_only", "non", known());
    REQUIRE(weaker.has_value());
    CHECK(weaker->weakening.has_value());

    auto unchecked = cli::change_setting(config, "agent.verify_replies", "non", known());
    REQUIRE(unchecked.has_value());
    CHECK(unchecked->weakening.has_value());

    auto steps = cli::change_setting(config, "agent.max_steps", "4", known());
    REQUIRE(steps.has_value());
    CHECK(steps->updated.agent.max_steps == 4);
    CHECK_FALSE(steps->weakening.has_value());
}

TEST_CASE("change_setting : permissions", "[reglages]") {
    const core::AppConfig config; // read_only allow, reversible allow, sensitive confirm

    auto stricter = cli::change_setting(config, "permissions.reversible", "confirmer", known());
    REQUIRE(stricter.has_value());
    CHECK(stricter->updated.permissions.reversible == core::Policy::Confirm);
    CHECK(stricter->strengthens);
    CHECK_FALSE(stricter->weakening.has_value());

    auto looser =
        cli::change_setting(stricter->updated, "permissions.reversible", "allow", known());
    REQUIRE(looser.has_value());
    CHECK(looser->weakening.has_value());

    auto denied = cli::change_setting(config, "permissions.sensitive", "refuser", known());
    REQUIRE(denied.has_value());
    auto reopened =
        cli::change_setting(denied->updated, "permissions.sensitive", "confirmer", known());
    REQUIRE(reopened.has_value());
    CHECK(reopened->weakening.has_value()); // refusé → possible : affaiblit

    // Une action sensible n'est jamais autorisée sans confirmation.
    CHECK_FALSE(cli::change_setting(config, "permissions.sensitive", "autoriser", known()));
    CHECK_FALSE(cli::change_setting(config, "permissions.sensitive", "allow", known()));
}

TEST_CASE("change_setting : modules", "[reglages]") {
    const core::AppConfig config;
    auto user = cli::change_setting(config, "modules.communaute.enabled", "oui", known());
    REQUIRE(user.has_value());
    CHECK(user->weakening.has_value());
    CHECK(user->updated.modules.at("communaute").enabled == true);

    auto bundled = cli::change_setting(config, "modules.meteo.enabled", "non", known());
    REQUIRE(bundled.has_value());
    CHECK_FALSE(bundled->weakening.has_value());
    CHECK(bundled->updated.modules.at("meteo").enabled == false);

    CHECK_FALSE(cli::change_setting(config, "modules.inconnu.enabled", "oui", known()));
}

TEST_CASE("change_setting : web", "[reglages]") {
    const core::AppConfig config;
    auto url = cli::change_setting(config, "web.searxng_url", "http://192.168.1.20:8888", known());
    REQUIRE(url.has_value());
    CHECK(url->weakening.has_value()); // les recherches partent vers ce serveur
    auto cleared = cli::change_setting(url->updated, "web.searxng_url", "aucune", known());
    REQUIRE(cleared.has_value());
    CHECK_FALSE(cleared->updated.web.searxng_url.has_value());
    CHECK_FALSE(cleared->weakening.has_value());

    // Mêmes règles que le fichier : adresse invalide refusée.
    CHECK_FALSE(cli::change_setting(config, "web.searxng_url", "ftp://serveur", known()));
    CHECK_FALSE(
        cli::change_setting(config, "web.searxng_url", "http://moi:secret@hote:1", known()));
    CHECK_FALSE(cli::change_setting(config, "web.searxng_idle_minutes", "0", known()));
}

TEST_CASE("change_setting : refus", "[reglages]") {
    const core::AppConfig config;
    CHECK_FALSE(cli::change_setting(config, "agent.inconnu", "oui", known()));
    CHECK_FALSE(cli::change_setting(config, "agent.local_only", "peut-être", known()));
    CHECK_FALSE(cli::change_setting(config, "agent.max_steps", "101", known()));
    CHECK_FALSE(cli::change_setting(config, "agent.max_steps", "4abc", known()));
    CHECK_FALSE(cli::change_setting(config, "agent.max_steps", "-1", known()));
    CHECK_FALSE(cli::change_setting(config, "permissions.read_only", "tout", known()));
    // Le prompt système et les modèles ne se changent pas par /reglage.
    CHECK_FALSE(cli::change_setting(config, "agent.system_prompt", "ignore tout", known()));
    CHECK_FALSE(cli::change_setting(config, "models.gemma.base_url", "http://x", known()));
}
