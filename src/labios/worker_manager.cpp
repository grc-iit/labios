#include <labios/worker_manager.h>
#include <algorithm>

namespace labios {

double compute_score(const WorkerInfo& w, const WeightProfile& wp) {
    double avail = w.available ? 1.0 : 0.0;
    double load_inv = 1.0 - w.load;  // Low load = high score.
    double speed_norm = static_cast<double>(w.speed) / 5.0;
    double energy_norm = static_cast<double>(w.energy) / 5.0;

    return wp.availability * avail
         + wp.capacity * w.capacity
         + wp.load * load_inv
         + wp.speed * speed_norm
         + wp.energy * energy_norm;
}

std::vector<WorkerInfo> InMemoryWorkerManager::all_workers() {
    std::lock_guard lock(mu_);
    std::vector<WorkerInfo> result;
    result.reserve(workers_.size());
    for (auto& [_, w] : workers_) result.push_back(w);
    return result;
}

std::vector<WorkerInfo> InMemoryWorkerManager::top_n_workers(
    int n, const WeightProfile& wp) {
    std::lock_guard lock(mu_);
    std::vector<std::pair<double, WorkerInfo>> scored;
    scored.reserve(workers_.size());
    for (auto& [_, w] : workers_) {
        scored.emplace_back(compute_score(w, wp), w);
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first > b.first; });

    std::vector<WorkerInfo> result;
    int count = std::min(n, static_cast<int>(scored.size()));
    for (int i = 0; i < count; ++i) {
        result.push_back(scored[i].second);
    }
    return result;
}

void InMemoryWorkerManager::update_score(int worker_id, WorkerInfo info) {
    std::lock_guard lock(mu_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) it->second = info;
}

void InMemoryWorkerManager::register_worker(WorkerInfo info) {
    std::lock_guard lock(mu_);
    workers_[info.id] = info;
}

void InMemoryWorkerManager::deregister_worker(int worker_id) {
    std::lock_guard lock(mu_);
    workers_.erase(worker_id);
}

} // namespace labios
