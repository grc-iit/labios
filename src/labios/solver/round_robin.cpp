#include <labios/solver/round_robin.h>

#include <algorithm>

namespace labios {

auto RoundRobinSolver::assign(std::vector<std::vector<std::byte>> labels,
                              const std::vector<WorkerInfo>& workers)
    -> AssignmentMap {

    // Filter to available workers only.
    std::vector<int> available_ids;
    available_ids.reserve(workers.size());
    for (const auto& worker : workers) {
        if (worker.available) {
            available_ids.push_back(worker.id);
        }
    }

    if (available_ids.empty()) {
        return {};
    }

    // Wrap the cursor if it exceeds the available pool size.
    if (next_ >= available_ids.size()) {
        next_ = 0;
    }

    AssignmentMap result;
    result.reserve(std::min(labels.size(), available_ids.size()));
    for (auto& label : labels) {
        result[available_ids[next_]].push_back(std::move(label));
        next_ = (next_ + 1) % available_ids.size();
    }

    return result;
}

} // namespace labios
