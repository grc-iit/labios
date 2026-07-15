#include <catch2/catch_test_macros.hpp>
#include <labios/catalog_manager.h>
#include <labios/label.h>

TEST_CASE("parking decision history is bounded without dropping retry accounting", "[dispatcher][parking]") {
    labios::LabelData label;
    label.id = 42;
    label.type = labios::LabelType::Write;
    for (uint32_t attempt = 1; attempt <= 50; ++attempt) {
        labios::SchedulingDecisionSnapshot decision;
        decision.attempt = attempt;
        decision.outcome = "Parked";
        decision.park_reason = "NO_WORKERS";
        label.score_snapshot.decisions.push_back(std::move(decision));
        labios::bound_decision_history(label);
    }

    uint64_t previous_delay = 0;
    for (uint64_t attempt = 1; attempt <= 50; ++attempt) {
        const auto delay = labios::parking_backoff_ms(attempt);
        CHECK(delay >= previous_delay);
        CHECK(delay >= 100);
        CHECK(delay <= 60'000);
        previous_delay = delay;
    }

    REQUIRE(label.score_snapshot.decisions.size() == 32);
    REQUIRE(label.score_snapshot.decisions.front().attempt == 19);
    REQUIRE(label.score_snapshot.decisions.back().attempt == 50);
    REQUIRE(labios::serialize_label(label).size() < 64 * 1024);
}

TEST_CASE("malformed label input is classified rather than thrown as an unknown exception", "[dispatcher][transport]") {
    const std::byte malformed[] = {std::byte{0x01}, std::byte{0x02}};
    REQUIRE_THROWS_AS(labios::deserialize_label(malformed), labios::LabelDecodeError);
}
