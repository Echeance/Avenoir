#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/config.hpp"
#include "avenoir/core/permission.hpp"

#include "settings_catalog.hpp"

using namespace avenoir;
using daemon::KnownModule;

namespace {

/// Un module fourni avec Avenoir, un installé par l'utilisateur.
std::vector<KnownModule> known() {
    return {{.id = "meteo", .bundled = true, .permissions = {"network"}},
            {.id = "communaute", .bundled = false, .permissions = {"network", "files.read"}}};
}

} // namespace

TEST_CASE("list_settings : valeurs actuelles en français", "[reglages]") {
    core::AppConfig config;
    config.agent.local_only = true;
    const auto lines = daemon::list_settings(config, known());
    const auto find = [&lines](const std::string& key) {
        return *std::ranges::find(lines, key, &daemon::SettingLine::key);
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

    auto stronger = daemon::change_setting(config, "agent.local_only", "oui", known());
    REQUIRE(stronger.has_value());
    CHECK(stronger->updated.agent.local_only);
    CHECK_FALSE(stronger->weakening.has_value());
    CHECK(stronger->strengthens);
    CHECK(stronger->before == "non");
    CHECK(stronger->after == "oui");

    auto weaker = daemon::change_setting(stronger->updated, "agent.local_only", "non", known());
    REQUIRE(weaker.has_value());
    CHECK(weaker->weakening.has_value());

    auto unchecked = daemon::change_setting(config, "agent.verify_replies", "non", known());
    REQUIRE(unchecked.has_value());
    CHECK(unchecked->weakening.has_value());

    auto steps = daemon::change_setting(config, "agent.max_steps", "4", known());
    REQUIRE(steps.has_value());
    CHECK(steps->updated.agent.max_steps == 4);
    CHECK_FALSE(steps->weakening.has_value());
}

TEST_CASE("change_setting : permissions", "[reglages]") {
    const core::AppConfig config; // read_only allow, reversible allow, sensitive confirm

    auto stricter = daemon::change_setting(config, "permissions.reversible", "confirmer", known());
    REQUIRE(stricter.has_value());
    CHECK(stricter->updated.permissions.reversible == core::Policy::Confirm);
    CHECK(stricter->strengthens);
    CHECK_FALSE(stricter->weakening.has_value());

    auto looser =
        daemon::change_setting(stricter->updated, "permissions.reversible", "allow", known());
    REQUIRE(looser.has_value());
    CHECK(looser->weakening.has_value());

    auto denied = daemon::change_setting(config, "permissions.sensitive", "refuser", known());
    REQUIRE(denied.has_value());
    auto reopened =
        daemon::change_setting(denied->updated, "permissions.sensitive", "confirmer", known());
    REQUIRE(reopened.has_value());
    CHECK(reopened->weakening.has_value()); // refusé → possible : affaiblit

    // « autoriser » pour les actions sensibles : possible, mais seulement comme
    // un affaiblissement (avertissement puis « Je sais ce que je fais »).
    auto allowed = daemon::change_setting(config, "permissions.sensitive", "autoriser", known());
    REQUIRE(allowed.has_value());
    CHECK(allowed->updated.permissions.sensitive == core::Policy::Allow);
    const std::string warning = allowed->weakening.value_or("");
    CHECK(warning.contains("SANS vous demander"));
    CHECK(warning.contains("commandes shell"));
}

TEST_CASE("change_setting : modules", "[reglages]") {
    const core::AppConfig config;
    auto user = daemon::change_setting(config, "modules.communaute.enabled", "oui", known());
    REQUIRE(user.has_value());
    CHECK(user->weakening.has_value());
    CHECK(user->updated.modules.at("communaute").enabled == true);

    auto bundled = daemon::change_setting(config, "modules.meteo.enabled", "non", known());
    REQUIRE(bundled.has_value());
    CHECK_FALSE(bundled->weakening.has_value());
    CHECK(bundled->updated.modules.at("meteo").enabled == false);

    CHECK_FALSE(daemon::change_setting(config, "modules.inconnu.enabled", "oui", known()));
}

TEST_CASE("change_setting : web", "[reglages]") {
    const core::AppConfig config;
    auto url =
        daemon::change_setting(config, "web.searxng_url", "http://192.168.1.20:8888", known());
    REQUIRE(url.has_value());
    CHECK(url->weakening.has_value()); // les recherches partent vers ce serveur
    auto cleared = daemon::change_setting(url->updated, "web.searxng_url", "aucune", known());
    REQUIRE(cleared.has_value());
    CHECK_FALSE(cleared->updated.web.searxng_url.has_value());
    CHECK_FALSE(cleared->weakening.has_value());

    // Mêmes règles que le fichier : adresse invalide refusée.
    CHECK_FALSE(daemon::change_setting(config, "web.searxng_url", "ftp://serveur", known()));
    CHECK_FALSE(
        daemon::change_setting(config, "web.searxng_url", "http://moi:secret@hote:1", known()));
    CHECK_FALSE(daemon::change_setting(config, "web.searxng_idle_minutes", "0", known()));
}

TEST_CASE("change_setting : refus", "[reglages]") {
    const core::AppConfig config;
    CHECK_FALSE(daemon::change_setting(config, "agent.inconnu", "oui", known()));
    CHECK_FALSE(daemon::change_setting(config, "agent.local_only", "peut-être", known()));
    CHECK_FALSE(daemon::change_setting(config, "agent.max_steps", "101", known()));
    CHECK_FALSE(daemon::change_setting(config, "agent.max_steps", "4abc", known()));
    CHECK_FALSE(daemon::change_setting(config, "agent.max_steps", "-1", known()));
    CHECK_FALSE(daemon::change_setting(config, "permissions.read_only", "tout", known()));
    // Le prompt système et les modèles ne se changent pas par /reglage.
    CHECK_FALSE(daemon::change_setting(config, "agent.system_prompt", "ignore tout", known()));
    CHECK_FALSE(daemon::change_setting(config, "models.gemma.base_url", "http://x", known()));
}

TEST_CASE("change_setting : orchestration", "[reglages]") {
    const core::AppConfig config;
    auto mode = daemon::change_setting(config, "orchestration.mode", "configured", known());
    REQUIRE(mode.has_value());
    CHECK(mode->updated.orchestration.mode == "configured");
    auto parallel = daemon::change_setting(config, "orchestration.max_parallel", "2", known());
    REQUIRE(parallel.has_value());
    CHECK(parallel->updated.orchestration.max_parallel == 2);
    auto off = daemon::change_setting(config, "orchestration.enabled", "non", known());
    REQUIRE(off.has_value());
    CHECK_FALSE(off->updated.orchestration.enabled);
    CHECK_FALSE(daemon::change_setting(config, "orchestration.mode", "libre", known()));
    CHECK_FALSE(daemon::change_setting(config, "orchestration.max_parallel", "9", known()));
}

TEST_CASE("change_setting : activer un module installé accorde ses permissions",
          "[reglages][permissions]") {
    const core::AppConfig config;
    auto enabled = daemon::change_setting(config, "modules.communaute.enabled", "oui", known());
    REQUIRE(enabled.has_value());
    REQUIRE(enabled->weakening.has_value());
    CHECK(enabled->weakening.value_or("").contains("réseau, lecture de fichiers"));
    CHECK(enabled->updated.modules.at("communaute").granted ==
          std::vector<std::string>{"network", "files.read"});

    // Nouvelle version qui demande plus : « oui » de nouveau est un changement.
    core::AppConfig older = enabled->updated;
    older.modules.at("communaute").granted = {"network"};
    auto regrant = daemon::change_setting(older, "modules.communaute.enabled", "oui", known());
    REQUIRE(regrant.has_value());
    CHECK(regrant->before != regrant->after);
    CHECK(regrant->weakening.has_value());
    CHECK(regrant->updated.modules.at("communaute").granted.size() == 2);

    // Déjà tout accordé : rien à changer.
    auto same =
        daemon::change_setting(enabled->updated, "modules.communaute.enabled", "oui", known());
    REQUIRE(same.has_value());
    CHECK(same->before == same->after);

    // Le désactiver retire l'accord : le réactiver le redemandera.
    auto disabled =
        daemon::change_setting(enabled->updated, "modules.communaute.enabled", "non", known());
    REQUIRE(disabled.has_value());
    CHECK(disabled->strengthens);
    CHECK(disabled->updated.modules.at("communaute").granted.empty());

    // Module fourni : aucun accord à enregistrer.
    auto bundled = daemon::change_setting(config, "modules.meteo.enabled", "non", known());
    REQUIRE(bundled.has_value());
    CHECK(bundled->updated.modules.at("meteo").granted.empty());
}

TEST_CASE("change_setting : conservation du journal", "[reglages][journal]") {
    const core::AppConfig config;
    auto days = daemon::change_setting(config, "journal.retention_days", "30", known());
    REQUIRE(days.has_value());
    CHECK(days->updated.journal.retention_days == 30);
    CHECK(days->before == "90");
    CHECK_FALSE(days->weakening.has_value());
    CHECK_FALSE(daemon::change_setting(config, "journal.retention_days", "0", known()));
    CHECK_FALSE(daemon::change_setting(config, "journal.retention_days", "beaucoup", known()));
}
