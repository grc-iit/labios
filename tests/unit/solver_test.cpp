#include <catch2/catch_test_macros.hpp>
#include <labios/solver/round_robin.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

auto fake_label(uint8_t tag) -> std::vector<std::byte> {
    return {static_cast<std::byte>(tag)};
}

} // namespace

TEST_CASE("RoundRobin distributes evenly across workers", "[solver]") {
    labios::RoundRobinSolver solver;

    std::vector<labios::WorkerInfo> workers = {
        {.id = 1}, {.id = 2}, {.id = 3}};

    std::vector<std::vector<std::byte>> labels;
    for (uint8_t i = 0; i < 6; ++i) {
        labels.push_back(fake_label(i));
    }

    auto result = solver.assign(std::move(labels), std::move(workers));

    REQUIRE(result.size() == 3);
    CHECK(result[1].size() == 2);
    CHECK(result[2].size() == 2);
    CHECK(result[3].size() == 2);
}

TEST_CASE("RoundRobin skips unavailable workers", "[solver]") {
    labios::RoundRobinSolver solver;

    std::vector<labios::WorkerInfo> workers = {
        {.id = 1, .available = true},
        {.id = 2, .available = false},
        {.id = 3, .available = true}};

    std::vector<std::vector<std::byte>> labels;
    for (uint8_t i = 0; i < 4; ++i) {
        labels.push_back(fake_label(i));
    }

    auto result = solver.assign(std::move(labels), std::move(workers));

    REQUIRE(result.size() == 2);
    CHECK(result.count(2) == 0);
    CHECK(result[1].size() == 2);
    CHECK(result[3].size() == 2);
}

TEST_CASE("RoundRobin with no available workers returns empty", "[solver]") {
    labios::RoundRobinSolver solver;

    std::vector<labios::WorkerInfo> workers = {
        {.id = 1, .available = false},
        {.id = 2, .available = false}};

    std::vector<std::vector<std::byte>> labels;
    labels.push_back(fake_label(0));
    labels.push_back(fake_label(1));

    auto result = solver.assign(std::move(labels), std::move(workers));

    CHECK(result.empty());
}
