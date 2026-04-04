#include <labios/solver/constraint.h>
#include <labios/worker_manager.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Constraint solver routes to highest-scored workers", "[solver]") {
    labios::WeightProfile wp{"high_bandwidth", 0.0, 0.15, 0.15, 0.70, 0.0};
    labios::ConstraintSolver solver(wp);

    std::vector<labios::WorkerInfo> workers = {
        {1, true, 0.9, 0.1, 5, 1},  // fast NVMe
        {2, true, 0.5, 0.5, 3, 3},  // SSD
        {3, true, 0.2, 0.8, 1, 5},  // HDD
    };

    std::vector<std::vector<std::byte>> labels(10);
    auto result = solver.assign(std::move(labels), workers);

    size_t total = 0;
    for (auto& [wid, payloads] : result) total += payloads.size();
    CHECK(total == 10);

    // Worker 1 should be present (highest score).
    CHECK(result.count(1) > 0);
}

TEST_CASE("Constraint solver skips unavailable workers", "[solver]") {
    labios::WeightProfile wp{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0};
    labios::ConstraintSolver solver(wp);

    std::vector<labios::WorkerInfo> workers = {
        {1, false, 0.9, 0.1, 5, 1},  // unavailable
        {2, true, 0.5, 0.5, 3, 3},
        {3, true, 0.2, 0.8, 1, 5},
    };

    std::vector<std::vector<std::byte>> labels(6);
    auto result = solver.assign(std::move(labels), workers);

    // Worker 1 is unavailable. Under low_latency (availability weight = 0.5),
    // workers 2 and 3 should get all labels.
    CHECK(result.count(1) == 0);
}

TEST_CASE("Constraint solver empty workers returns empty", "[solver]") {
    labios::WeightProfile wp{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0};
    labios::ConstraintSolver solver(wp);
    std::vector<std::vector<std::byte>> labels(5);
    auto result = solver.assign(std::move(labels), {});
    CHECK(result.empty());
}
