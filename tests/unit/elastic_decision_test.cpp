#include <labios/elastic/decision_engine.h>
#include <catch2/catch_test_macros.hpp>

using namespace labios::elastic;
using clk = std::chrono::steady_clock;

static ElasticSnapshot base_snapshot() {
    return {
        .pressure_count = 0,
        .pressure_threshold = 5,
        .current_workers = 2,
        .min_workers = 1,
        .max_workers = 5,
        .idle_worker_ids = {},
        .suspended_worker_ids = {},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::seconds(5),
    };
}

TEST_CASE("Commission when pressure exceeds threshold", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 5;
    snap.current_workers = 1;
    auto d = evaluate(snap);
    CHECK(d.action == Action::Commission);
}

TEST_CASE("No commission below pressure threshold", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 3;
    auto d = evaluate(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("No commission at max workers", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 10;
    snap.current_workers = 5;
    snap.max_workers = 5;
    auto d = evaluate(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("Resume preferred over commission when suspended workers exist", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 5;
    snap.suspended_worker_ids = {101, 102};
    auto d = evaluate(snap);
    CHECK(d.action == Action::Resume);
    CHECK(d.target_worker_id == 101);
}

TEST_CASE("Cooldown prevents rapid commission", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 5;
    snap.last_commission = clk::now() - std::chrono::seconds(1);
    snap.cooldown = std::chrono::seconds(5);
    auto d = evaluate(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("Decommission idle worker when above minimum and no pressure", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 0;
    snap.current_workers = 3;
    snap.idle_worker_ids = {102};
    auto d = evaluate(snap);
    CHECK(d.action == Action::Decommission);
    CHECK(d.target_worker_id == 102);
}

TEST_CASE("No decommission at minimum workers", "[elastic]") {
    auto snap = base_snapshot();
    snap.current_workers = 1;
    snap.min_workers = 1;
    snap.idle_worker_ids = {1};
    auto d = evaluate(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("No decommission while queue has pressure", "[elastic]") {
    auto snap = base_snapshot();
    snap.pressure_count = 3;
    snap.idle_worker_ids = {102};
    auto d = evaluate(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("Energy budget triggers decommission of idle worker", "[elastic]") {
    auto snap = base_snapshot();
    snap.energy_budget = 10.0;
    snap.current_energy = 12.0;
    snap.idle_worker_ids = {103};
    auto d = evaluate(snap);
    CHECK(d.action == Action::Decommission);
    CHECK(d.target_worker_id == 103);
}

TEST_CASE("Energy budget returns None when no idle workers", "[elastic]") {
    auto snap = base_snapshot();
    snap.energy_budget = 10.0;
    snap.current_energy = 12.0;
    snap.idle_worker_ids = {};
    auto d = evaluate(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("Zero energy budget disables energy check", "[elastic]") {
    auto snap = base_snapshot();
    snap.energy_budget = 0.0;
    snap.current_energy = 999.0;
    snap.pressure_count = 5;
    auto d = evaluate(snap);
    CHECK(d.action == Action::Commission);
}

TEST_CASE("Evaluate handles zero workers gracefully", "[elastic]") {
    ElasticSnapshot snap{
        .pressure_count = 10,
        .pressure_threshold = 5,
        .current_workers = 0,
        .min_workers = 0,
        .max_workers = 5,
        .idle_worker_ids = {},
        .suspended_worker_ids = {},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::seconds(5),
    };
    auto d = evaluate(snap);
    CHECK(d.action == Action::Commission);
}
