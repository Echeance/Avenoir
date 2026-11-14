#include <catch2/catch_test_macros.hpp>

#include "avenoir/core/permission.hpp"

using avenoir::core::PermissionPolicy;
using avenoir::core::Policy;
using avenoir::core::RiskLevel;

TEST_CASE("politique par défaut", "[permission]") {
    const PermissionPolicy policy;
    CHECK(policy.for_risk(RiskLevel::ReadOnly) == Policy::Allow);
    CHECK(policy.for_risk(RiskLevel::Reversible) == Policy::Allow);
    CHECK(policy.for_risk(RiskLevel::Sensitive) == Policy::Confirm);
}

TEST_CASE("sensitive allow : choisi par l'utilisateur, respecté", "[permission]") {
    // Le réglage n'est accepté qu'après « Je sais ce que je fais » (décision 59).
    const PermissionPolicy policy{.read_only = Policy::Allow,
                                  .reversible = Policy::Allow,
                                  .sensitive = Policy::Allow,
                                  .tools = {}};
    CHECK(policy.for_risk(RiskLevel::Sensitive) == Policy::Allow);
}

TEST_CASE("deny reste deny", "[permission]") {
    const PermissionPolicy policy{.read_only = Policy::Deny,
                                  .reversible = Policy::Deny,
                                  .sensitive = Policy::Deny,
                                  .tools = {}};
    CHECK(policy.for_risk(RiskLevel::ReadOnly) == Policy::Deny);
    CHECK(policy.for_risk(RiskLevel::Sensitive) == Policy::Deny);
}

TEST_CASE("politiques : conversion texte aller-retour", "[permission]") {
    for (const auto policy : {Policy::Allow, Policy::Confirm, Policy::Deny}) {
        CHECK(avenoir::core::policy_from_string(avenoir::core::to_string(policy)) == policy);
    }
    CHECK_FALSE(avenoir::core::policy_from_string("peut-être").has_value());
}

TEST_CASE("apply_rule : une règle ne lève qu'une confirmation", "[permission][rules]") {
    using avenoir::core::apply_rule;
    using avenoir::core::ToolRule;
    const ToolRule allow{.policy = Policy::Allow, .risk = RiskLevel::Reversible, .version = {}};
    const ToolRule deny{.policy = Policy::Deny, .risk = RiskLevel::Sensitive, .version = {}};
    CHECK(apply_rule(nullptr, RiskLevel::Sensitive, Policy::Confirm) == Policy::Confirm);
    CHECK(apply_rule(&allow, RiskLevel::Reversible, Policy::Confirm) == Policy::Allow);
    CHECK(apply_rule(&allow, RiskLevel::ReadOnly, Policy::Confirm) == Policy::Allow);
    // Risque relevé depuis l'accord : la règle ne vaut plus.
    CHECK(apply_rule(&allow, RiskLevel::Sensitive, Policy::Confirm) == Policy::Confirm);
    // Jamais au-delà d'un refus de la politique.
    CHECK(apply_rule(&allow, RiskLevel::Reversible, Policy::Deny) == Policy::Deny);
    CHECK(apply_rule(&deny, RiskLevel::ReadOnly, Policy::Allow) == Policy::Deny);
}

TEST_CASE("ToolRules : posées, retirées, remplacées", "[permission][rules]") {
    using avenoir::core::ToolRule;
    using avenoir::core::ToolRules;
    ToolRules rules;
    CHECK_FALSE(rules.find("a").has_value());
    rules.set("a", ToolRule{.policy = Policy::Deny, .risk = RiskLevel::Sensitive, .version = {}});
    CHECK(
        rules.find("a")
            .value_or(ToolRule{.policy = Policy::Allow, .risk = RiskLevel::ReadOnly, .version = {}})
            .policy == Policy::Deny);
    CHECK(rules.remove("a"));
    CHECK_FALSE(rules.remove("a"));
    rules.replace({{"b", ToolRule{}}});
    CHECK(rules.all().size() == 1);
}
