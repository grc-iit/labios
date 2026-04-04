#pragma once
#include <labios/solver/solver.h>

#include <array>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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

/// In-memory worker manager with mutex-protected registry and bucket-sorted
/// worker list. Workers are divided into 5 buckets by score range (paper
/// Section 3.2.3). A score update only moves the affected worker between
/// buckets, avoiding a full O(N log N) sort on every query.
class InMemoryWorkerManager {
public:
    static constexpr int kNumBuckets = 5;

    std::vector<WorkerInfo> all_workers();
    std::vector<WorkerInfo> top_n_workers(int n, const WeightProfile& wp);
    void update_score(int worker_id, WorkerInfo info);
    void register_worker(WorkerInfo info);
    void deregister_worker(int worker_id);

private:
    static int bucket_index(double score);
    void place_in_bucket(int worker_id, double score);
    void remove_from_buckets(int worker_id);

    std::mutex mu_;
    std::unordered_map<int, WorkerInfo> workers_;
    // Cached score per worker for the last-used weight profile.
    std::unordered_map<int, double> scores_;
    // Buckets: index 0 = lowest scores (0.0-0.2), index 4 = highest (0.8-1.0).
    std::array<std::unordered_set<int>, kNumBuckets> buckets_;
    WeightProfile last_profile_;
};

static_assert(WorkerManager<InMemoryWorkerManager>);

} // namespace labios
