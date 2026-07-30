#include <labios/solver/random.h>

#include <algorithm>

namespace labios {

AssignmentMap RandomSolver::assign(std::vector<std::vector<std::byte>> labels,
                                   const std::vector<WorkerInfo>& workers) {
    std::vector<const WorkerInfo*> available;
    for (const auto& worker : workers) {
        if (worker.available) available.push_back(&worker);
    }
    if (available.empty()) return {};
    std::uniform_int_distribution<size_t> dist(0, available.size() - 1);

    AssignmentMap result;
    result.reserve(std::min(labels.size(), available.size()));
    for (auto& label : labels) {
        result[available[dist(rng_)]->id].push_back(std::move(label));
    }
    return result;
}

} // namespace labios
