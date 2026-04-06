#include <labios/solver/random.h>
#include <catch2/catch_test_macros.hpp>
#include <set>

TEST_CASE("Random distributes to all workers including suspended", "[solver]") {
    labios::RandomSolver solver;
    std::vector<labios::WorkerInfo> workers = {
        {1, true}, {2, false}, {3, true}
    };

    // Create 30 labels (enough for statistical coverage).
    std::vector<std::vector<std::byte>> labels(30);

    auto result = solver.assign(std::move(labels), workers);

    // Paper says: "distribute labels to all workers randomly regardless of
    // their state (i.e., active or suspended)."
    std::set<int> assigned_workers;
    size_t total = 0;
    for (auto& [wid, payloads] : result) {
        assigned_workers.insert(wid);
        total += payloads.size();
    }
    CHECK(total == 30);
    // With 30 labels and 3 workers, probability of missing one is < 0.001.
    CHECK(assigned_workers.size() == 3);
}

TEST_CASE("Random with no workers returns empty", "[solver]") {
    labios::RandomSolver solver;
    std::vector<std::vector<std::byte>> labels(5);
    auto result = solver.assign(std::move(labels), {});
    CHECK(result.empty());
}
