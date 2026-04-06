#include <labios/solver/minmax.h>
#include <catch2/catch_test_macros.hpp>
#include <set>

TEST_CASE("MinMax prefers fast workers for performance", "[solver]") {
    labios::MinMaxSolver solver;

    std::vector<labios::WorkerInfo> workers = {
        {1, true, 0.9, 0.1, 5, 1},  // fast, low energy
        {2, true, 0.5, 0.5, 3, 3},  // medium
        {3, true, 0.2, 0.8, 1, 5},  // slow, high energy
    };

    std::vector<std::vector<std::byte>> labels(6);
    auto result = solver.assign(std::move(labels), workers);

    size_t total = 0;
    for (auto& [wid, payloads] : result) total += payloads.size();
    CHECK(total == 6);

    // Worker 1 (speed=5, energy=1) has best performance/energy ratio
    // and should get more labels than worker 3.
    size_t w1_count = result.count(1) ? result[1].size() : 0;
    size_t w3_count = result.count(3) ? result[3].size() : 0;
    CHECK(w1_count >= w3_count);
}

TEST_CASE("MinMax respects capacity constraints", "[solver]") {
    labios::MinMaxSolver solver;

    std::vector<labios::WorkerInfo> workers = {
        {1, true, 0.01, 0.9, 5, 1},  // almost full
        {2, true, 0.9, 0.1, 1, 5},   // plenty of room
    };

    std::vector<std::vector<std::byte>> labels(10);
    auto result = solver.assign(std::move(labels), workers);

    // Worker 1 has almost no capacity. Worker 2 should get more.
    size_t w2_count = result.count(2) ? result[2].size() : 0;
    CHECK(w2_count >= 5);
}

TEST_CASE("MinMax with single worker assigns all labels", "[solver]") {
    labios::MinMaxSolver solver;
    std::vector<labios::WorkerInfo> workers = {{1, true, 0.5, 0.5, 3, 3}};
    std::vector<std::vector<std::byte>> labels(5);
    auto result = solver.assign(std::move(labels), workers);
    REQUIRE(result.size() == 1);
    CHECK(result[1].size() == 5);
}

TEST_CASE("MinMax with empty workers returns empty", "[solver]") {
    labios::MinMaxSolver solver;
    std::vector<std::vector<std::byte>> labels(5);
    auto result = solver.assign(std::move(labels), {});
    CHECK(result.empty());
}
