#pragma once
#include <labios/solver/solver.h>
#include <random>

namespace labios {

class RandomSolver {
public:
    AssignmentMap assign(std::vector<std::vector<std::byte>> labels,
                         std::vector<WorkerInfo> workers);
private:
    std::mt19937 rng_{std::random_device{}()};
};

static_assert(Solver<RandomSolver>);

} // namespace labios
