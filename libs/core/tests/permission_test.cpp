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
    const PermissionPolicy policy{
        .read_only = Policy::Allow, .reversible = Policy::Allow, .sensitive = Policy::Allow};
    CHECK(policy.for_risk(RiskLevel::Sensitive) == Policy::Allow);
}

TEST_CASE("deny reste deny", "[permission]") {
    const PermissionPolicy policy{
        .read_only = Policy::Deny, .reversible = Policy::Deny, .sensitive = Policy::Deny};
    CHECK(policy.for_risk(RiskLevel::ReadOnly) == Policy::Deny);
    CHECK(policy.for_risk(RiskLevel::Sensitive) == Policy::Deny);
}

TEST_CASE("politiques : conversion texte aller-retour", "[permission]") {
    for (const auto policy : {Policy::Allow, Policy::Confirm, Policy::Deny}) {
        CHECK(avenoir::core::policy_from_string(avenoir::core::to_string(policy)) == policy);
    }
    CHECK_FALSE(avenoir::core::policy_from_string("peut-être").has_value());
}
