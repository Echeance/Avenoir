#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/tool.hpp"

#include "ui.hpp"

using namespace avenoir;

TEST_CASE("ProjectUi : en arrière-plan, rien ne se confirme", "[projets]") {
    daemon::ProjectUi ui("site");
    // Personne pour répondre : toute question vaut « non », toute action à
    // confirmer est refusée (refus automatique hors présence).
    CHECK_FALSE(ui.ask("Votre nom ?").has_value());
    CHECK_FALSE(ui.ask_yes_no("Continuer ? [o/N]"));
    const core::ToolSpec spec{.name = "set_volume",
                              .description = "",
                              .input_schema = nlohmann::json::object(),
                              .risk = core::RiskLevel::Reversible};
    const core::ToolCall call{.id = "1", .name = "set_volume", .arguments = {{"level", 80}}};
    CHECK_FALSE(ui.confirm(spec, call));
    CHECK(ui.last_reply().empty());
    ui.assistant("Site prêt.");
    CHECK(ui.last_reply() == "Site prêt.");
}
