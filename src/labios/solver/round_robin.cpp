#include <labios/solver/round_robin.h>

#include <algorithm>

namespace labios {

auto RoundRobinSolver::assign(std::vector<std::vector<std::byte>> labels,
                               std::vector<WorkerInfo> workers)
    -> AssignmentMap {

    // Filter to available workers only.
    std::vector<WorkerInfo> available;
    std::copy_if(workers.begin(), workers.end(),
                 std::back_inserter(available),
                 [](const WorkerInfo& w) { return w.available; });

    if (available.empty()) {
        return {};
    }

    // Wrap the cursor if it exceeds the available pool size.
    if (next_ >= available.size()) {
        next_ = 0;
    }

    AssignmentMap result;
    for (auto& label : labels) {
        result[available[next_].id].push_back(std::move(label));
        next_ = (next_ + 1) % available.size();
    }

    return result;
}

} // namespace labios
