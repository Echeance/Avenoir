#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "avenoir/core/tool.hpp"

namespace {

using avenoir::core::RiskLevel;
using avenoir::core::Tool;
using avenoir::core::ToolOutput;
using avenoir::core::ToolResult;
using avenoir::core::ToolSpec;

class NamedTool final : public Tool {
  public:
    explicit NamedTool(std::string name) : name_(std::move(name)) {}

    [[nodiscard]] ToolSpec spec() const override {
        return ToolSpec{.name = name_,
                        .description = "test",
                        .input_schema = nlohmann::json::object(),
                        .risk = RiskLevel::ReadOnly};
    }
    [[nodiscard]] ToolResult call(const nlohmann::json& /*arguments*/) const override {
        return ToolOutput{.content = "ok"};
    }

  private:
    std::string name_;
};

struct LaunchArgs {
    std::string name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LaunchArgs, name)

} // namespace

TEST_CASE("le registre refuse les doublons", "[tool]") {
    avenoir::core::ToolRegistry registry;
    REQUIRE(registry.add(std::make_shared<NamedTool>("current_time")).has_value());
    CHECK_FALSE(registry.add(std::make_shared<NamedTool>("current_time")).has_value());
    CHECK(registry.size() == 1);
    CHECK(registry.find("current_time") != nullptr);
    CHECK(registry.find("inconnu") == nullptr);
}

TEST_CASE("le registre refuse les noms invalides et les outils nuls", "[tool]") {
    avenoir::core::ToolRegistry registry;
    CHECK_FALSE(registry.add(std::make_shared<NamedTool>("nom avec espaces")).has_value());
    CHECK_FALSE(registry.add(std::make_shared<NamedTool>("")).has_value());
    CHECK_FALSE(registry.add(std::make_shared<NamedTool>(std::string(65, 'x'))).has_value());
    CHECK_FALSE(registry.add(nullptr).has_value());
    CHECK(registry.empty());
}

TEST_CASE("parse_args lit des arguments valides et signale les invalides", "[tool]") {
    const auto ok = avenoir::core::parse_args<LaunchArgs>(nlohmann::json{{"name", "firefox"}});
    REQUIRE(ok.has_value());
    CHECK(ok->name == "firefox");

    const auto missing = avenoir::core::parse_args<LaunchArgs>(nlohmann::json::object());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == avenoir::core::ToolErrorKind::InvalidArguments);

    const auto wrong_type = avenoir::core::parse_args<LaunchArgs>(nlohmann::json{{"name", 42}});
    CHECK_FALSE(wrong_type.has_value());
}

TEST_CASE("niveaux de risque : conversion texte aller-retour", "[tool]") {
    for (const auto risk : {RiskLevel::ReadOnly, RiskLevel::Reversible, RiskLevel::Sensitive}) {
        CHECK(avenoir::core::risk_level_from_string(avenoir::core::to_string(risk)) == risk);
    }
    CHECK_FALSE(avenoir::core::risk_level_from_string("dangereux").has_value());
}
