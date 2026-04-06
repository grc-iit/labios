#include <labios/solver/random.h>

#include <algorithm>

namespace labios {

AssignmentMap RandomSolver::assign(std::vector<std::vector<std::byte>> labels,
                                   const std::vector<WorkerInfo>& workers) {
    if (workers.empty()) return {};

    // Paper: "distribute labels to all workers randomly regardless of their
    // state (i.e., active or suspended)."
    std::uniform_int_distribution<size_t> dist(0, workers.size() - 1);

    AssignmentMap result;
    result.reserve(std::min(labels.size(), workers.size()));
    for (auto& label : labels) {
        result[workers[dist(rng_)].id].push_back(std::move(label));
    }
    return result;
}

} // namespace labios
