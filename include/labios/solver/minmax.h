#pragma once
#include <labios/solver/solver.h>

namespace labios {

/// Paper Section 3.3(d): "the dispatcher aims to find a label assignment
/// that maximizes I/O performance while minimizing the system's energy
/// consumption, subject to the remaining capacity and load of the workers;
/// essentially a minmax multidimensional knapsack problem."
///
/// Uses approximate DP per Bertsimas & Demir (2002).
class MinMaxSolver {
public:
    AssignmentMap assign(std::vector<std::vector<std::byte>> labels,
                         std::vector<WorkerInfo> workers);
};

static_assert(Solver<MinMaxSolver>);

} // namespace labios
