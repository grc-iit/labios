#pragma once
#include <labios/solver/solver.h>

namespace labios {

/// Paper Section 3.3(c): "the dispatcher will distribute labels to workers
/// based on the constraint with higher weight value. The number of workers
/// needed per a set of labels is automatically determined by LABIOS based
/// on the total aggregate I/O size."
class ConstraintSolver {
public:
    explicit ConstraintSolver(WeightProfile profile);

    AssignmentMap assign(std::vector<std::vector<std::byte>> labels,
                         std::vector<WorkerInfo> workers);

private:
    WeightProfile profile_;
};

static_assert(Solver<ConstraintSolver>);

} // namespace labios
