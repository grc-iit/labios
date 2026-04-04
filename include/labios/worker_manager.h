#pragma once
#include <labios/solver/solver.h>

#include <mutex>
#include <unordered_map>
#include <vector>

namespace labios {

/// Compute the composite score for a worker given a weight profile.
/// Paper formula: Score_worker(i) = sum(Weight_j * Variable_j) for j=1..5
/// Variables are normalized to [0,1]:
///   availability: 0 or 1
///   capacity: already [0,1]
///   load: inverted (1 - load) so low load = high score
///   speed: speed / 5.0
///   energy: energy / 5.0
double compute_score(const WorkerInfo& w, const WeightProfile& wp);

/// In-memory worker manager with mutex-protected registry.
/// Satisfies the WorkerManager concept from solver.h.
class InMemoryWorkerManager {
public:
    std::vector<WorkerInfo> all_workers();
    std::vector<WorkerInfo> top_n_workers(int n, const WeightProfile& wp);
    void update_score(int worker_id, WorkerInfo info);
    void register_worker(WorkerInfo info);
    void deregister_worker(int worker_id);

private:
    std::mutex mu_;
    std::unordered_map<int, WorkerInfo> workers_;
};

static_assert(WorkerManager<InMemoryWorkerManager>);

} // namespace labios
