#include <catch2/catch_test_macros.hpp>
#include <labios/solver/intent_profiles.h>

TEST_CASE("Intent profiles adjust weights", "[solver][intent]") {
    labios::WeightProfile base{"test", 0.2, 0.2, 0.2, 0.1, 0.1, 0.1};

    SECTION("Checkpoint boosts capacity and speed") {
        auto p = labios::profile_for_intent(base, labios::Intent::Checkpoint);
        REQUIRE(p.capacity >= 0.4);
        REQUIRE(p.speed >= 0.4);
        REQUIRE(p.tier == 0.0);
    }

    SECTION("Embedding boosts tier and speed") {
        auto p = labios::profile_for_intent(base, labios::Intent::Embedding);
        REQUIRE(p.tier >= 0.4);
        REQUIRE(p.speed >= 0.3);
    }

    SECTION("ReasoningTrace boosts tier") {
        auto p = labios::profile_for_intent(base, labios::Intent::ReasoningTrace);
        REQUIRE(p.tier >= 0.5);
    }

    SECTION("Cache boosts speed and load") {
        auto p = labios::profile_for_intent(base, labios::Intent::Cache);
        REQUIRE(p.speed >= 0.4);
        REQUIRE(p.load >= 0.3);
    }

    SECTION("Intermediate boosts availability") {
        auto p = labios::profile_for_intent(base, labios::Intent::Intermediate);
        REQUIRE(p.availability >= 0.5);
        REQUIRE(p.load >= 0.2);
    }

    SECTION("None returns base profile") {
        auto p = labios::profile_for_intent(base, labios::Intent::None);
        REQUIRE(p.capacity == base.capacity);
        REQUIRE(p.speed == base.speed);
        REQUIRE(p.tier == base.tier);
    }

    SECTION("Already-high weights are not reduced") {
        labios::WeightProfile high{"high", 0.9, 0.9, 0.9, 0.9, 0.9, 0.9};
        auto p = labios::profile_for_intent(high, labios::Intent::Checkpoint);
        REQUIRE(p.capacity == 0.9);
        REQUIRE(p.speed == 0.9);
        REQUIRE(p.tier == 0.0);  // Checkpoint zeroes tier
    }
}
