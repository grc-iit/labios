#include <labios/solver/random.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <set>

TEST_CASE("Random excludes unavailable workers", "[solver]") {
    labios::RandomSolver solver;
    std::vector<labios::WorkerInfo> workers = {
        {1, true}, {2, false}, {3, true}
    };

    // Create 30 labels (enough for statistical coverage).
    std::vector<std::vector<std::byte>> labels(30);

    auto result = solver.assign(std::move(labels), workers);

    std::set<int> assigned_workers;
    size_t total = 0;
    for (auto& [wid, payloads] : result) {
        assigned_workers.insert(wid);
        total += payloads.size();
    }
    CHECK(total == 30);
    CHECK_FALSE(assigned_workers.contains(2));
    CHECK(std::all_of(assigned_workers.begin(), assigned_workers.end(),
                      [](int id) { return id == 1 || id == 3; }));
}

TEST_CASE("Random with no workers returns empty", "[solver]") {
    labios::RandomSolver solver;
    std::vector<std::vector<std::byte>> labels(5);
    auto result = solver.assign(std::move(labels), {});
    CHECK(result.empty());
}
