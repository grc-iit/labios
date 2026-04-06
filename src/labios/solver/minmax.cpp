#include <labios/solver/minmax.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace labios {

AssignmentMap MinMaxSolver::assign(
    std::vector<std::vector<std::byte>> labels,
    const std::vector<WorkerInfo>& workers) {
    if (workers.empty() || labels.empty()) return {};

    // Compute a profit value for each worker: maximize speed, minimize energy,
    // subject to capacity and load constraints.
    // Profit(w) = (speed/5.0) * capacity * (1.0 - load) / max(energy/5.0, 0.01)
    // This captures the paper's objective: max performance, min energy,
    // subject to capacity and load.
    struct ScoredWorker {
        int id;
        double profit;
        double weight;  // inverse capacity: how "full" the worker is
    };

    std::vector<ScoredWorker> scored;
    scored.reserve(workers.size());
    for (auto& w : workers) {
        if (!w.available) continue;
        double speed_norm = static_cast<double>(w.speed) / 5.0;
        double energy_norm = std::max(static_cast<double>(w.energy) / 5.0, 0.01);
        double profit = speed_norm * w.capacity * (1.0 - w.load) / energy_norm;
        double weight = 1.0 - w.capacity;  // full workers are "heavy"
        scored.push_back({w.id, profit, weight});
    }

    if (scored.empty()) return {};

    // Sort by profit descending.
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.profit > b.profit; });

    // Greedy approximate DP: assign labels to workers proportional to profit.
    double total_profit = 0.0;
    for (auto& sw : scored) total_profit += sw.profit;

    AssignmentMap result;
    result.reserve(std::min(labels.size(), scored.size()));
    size_t assigned = 0;
    for (size_t i = 0; i < scored.size() && assigned < labels.size(); ++i) {
        double fraction = (total_profit > 0.0)
            ? scored[i].profit / total_profit
            : 1.0 / static_cast<double>(scored.size());
        size_t count = static_cast<size_t>(
            std::round(fraction * static_cast<double>(labels.size())));
        count = std::min(count, labels.size() - assigned);
        // Ensure at least 1 label if profit > 0 and labels remain.
        if (count == 0 && assigned < labels.size()) count = 1;

        for (size_t j = 0; j < count && assigned < labels.size(); ++j) {
            result[scored[i].id].push_back(std::move(labels[assigned++]));
        }
    }

    // Distribute any remaining labels to the highest-profit worker.
    while (assigned < labels.size()) {
        result[scored[0].id].push_back(std::move(labels[assigned++]));
    }

    return result;
}

} // namespace labios
