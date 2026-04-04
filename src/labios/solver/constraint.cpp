#include <labios/solver/constraint.h>
#include <labios/worker_manager.h>

#include <algorithm>
#include <cmath>

namespace labios {

ConstraintSolver::ConstraintSolver(WeightProfile profile)
    : profile_(std::move(profile)) {}

AssignmentMap ConstraintSolver::assign(
    std::vector<std::vector<std::byte>> labels,
    std::vector<WorkerInfo> workers) {
    if (workers.empty() || labels.empty()) return {};

    // Score and sort workers by the weight profile.
    std::vector<std::pair<double, WorkerInfo>> scored;
    scored.reserve(workers.size());
    for (auto& w : workers) {
        scored.emplace_back(compute_score(w, profile_), w);
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first > b.first; });

    // Determine how many workers to use. Paper: "The number of workers
    // needed per a set of labels is automatically determined by LABIOS."
    // Heuristic: ceil(label_count / labels_per_worker), where
    // labels_per_worker is tuned to avoid overloading any single worker.
    constexpr size_t labels_per_worker = 50;
    size_t n_workers = std::min(
        scored.size(),
        std::max(static_cast<size_t>(1),
                 (labels.size() + labels_per_worker - 1) / labels_per_worker));

    // Distribute labels evenly among top-N scored workers.
    AssignmentMap result;
    for (size_t i = 0; i < labels.size(); ++i) {
        size_t idx = i % n_workers;
        result[scored[idx].second.id].push_back(std::move(labels[i]));
    }
    return result;
}

} // namespace labios
