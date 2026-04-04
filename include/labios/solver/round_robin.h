#pragma once
#include <labios/solver/solver.h>

namespace labios {

class RoundRobinSolver {
public:
    AssignmentMap assign(std::vector<std::vector<std::byte>> labels,
                         std::vector<WorkerInfo> workers);

private:
    size_t next_ = 0;
};

static_assert(Solver<RoundRobinSolver>);

} // namespace labios
