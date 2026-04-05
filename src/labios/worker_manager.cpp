#include <labios/worker_manager.h>
#include <algorithm>
#include <ranges>

namespace labios {

double compute_score(const WorkerInfo& w, const WeightProfile& wp) {
    double avail = w.available ? 1.0 : 0.0;
    double load_inv = 1.0 - w.load;  // Low load = high score.
    double speed_norm = static_cast<double>(w.speed) / 5.0;
    double energy_norm = static_cast<double>(w.energy) / 5.0;
    double tier_norm = static_cast<double>(static_cast<int>(w.tier)) / 2.0;

    return wp.availability * avail
         + wp.capacity * w.capacity
         + wp.load * load_inv
         + wp.speed * speed_norm
         + wp.energy * energy_norm
         + wp.tier * tier_norm;
}

int InMemoryWorkerManager::bucket_index(double score) {
    // Buckets: [0.0, 0.2), [0.2, 0.4), [0.4, 0.6), [0.6, 0.8), [0.8, 1.0]
    int idx = static_cast<int>(score * kNumBuckets);
    return std::clamp(idx, 0, kNumBuckets - 1);
}

void InMemoryWorkerManager::place_in_bucket(int worker_id, double score) {
    scores_[worker_id] = score;
    buckets_[bucket_index(score)].insert(worker_id);
}

void InMemoryWorkerManager::remove_from_buckets(int worker_id) {
    auto it = scores_.find(worker_id);
    if (it != scores_.end()) {
        buckets_[bucket_index(it->second)].erase(worker_id);
        scores_.erase(it);
    }
}

std::vector<WorkerInfo> InMemoryWorkerManager::all_workers() {
    std::lock_guard lock(mu_);
    std::vector<WorkerInfo> result;
    result.reserve(workers_.size());
    std::ranges::transform(workers_, std::back_inserter(result),
                           [](auto& pair) { return pair.second; });
    return result;
}

std::vector<WorkerInfo> InMemoryWorkerManager::top_n_workers(
    int n, const WeightProfile& wp) {
    std::lock_guard lock(mu_);

    // If the profile changed, rebuild all bucket assignments.
    if (wp.name != last_profile_.name ||
        wp.availability != last_profile_.availability ||
        wp.capacity != last_profile_.capacity ||
        wp.load != last_profile_.load ||
        wp.speed != last_profile_.speed ||
        wp.energy != last_profile_.energy ||
        wp.tier != last_profile_.tier) {

        for (auto& bucket : buckets_) bucket.clear();
        scores_.clear();
        for (auto& [id, w] : workers_) {
            place_in_bucket(id, compute_score(w, wp));
        }
        last_profile_ = wp;
    }

    // Pick from highest bucket first.
    std::vector<WorkerInfo> result;
    int remaining = n;
    for (int b = kNumBuckets - 1; b >= 0 && remaining > 0; --b) {
        for (int wid : buckets_[b]) {
            if (remaining <= 0) break;
            auto it = workers_.find(wid);
            if (it != workers_.end()) {
                result.push_back(it->second);
                --remaining;
            }
        }
    }
    return result;
}

void InMemoryWorkerManager::update_score(int worker_id, WorkerInfo info) {
    std::lock_guard lock(mu_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        bool was_available = it->second.available;
        it->second = info;

        // Track suspension transitions.
        if (was_available && !info.available) {
            suspended_since_[worker_id] = std::chrono::steady_clock::now();
        } else if (!was_available && info.available) {
            suspended_since_.erase(worker_id);
        }

        // Re-bucket if we have a profile.
        if (!last_profile_.name.empty()) {
            remove_from_buckets(worker_id);
            place_in_bucket(worker_id, compute_score(info, last_profile_));
        }
    }
}

void InMemoryWorkerManager::register_worker(WorkerInfo info) {
    std::lock_guard lock(mu_);
    int id = info.id;
    workers_[id] = info;
    // Bucket only if a profile has been set (by a prior top_n_workers call).
    if (!last_profile_.name.empty()) {
        remove_from_buckets(id);
        place_in_bucket(id, compute_score(info, last_profile_));
    }
}

void InMemoryWorkerManager::deregister_worker(int worker_id) {
    std::lock_guard lock(mu_);
    remove_from_buckets(worker_id);
    workers_.erase(worker_id);
    suspended_since_.erase(worker_id);
}

size_t InMemoryWorkerManager::worker_count() {
    std::lock_guard lock(mu_);
    return workers_.size();
}

int InMemoryWorkerManager::next_worker_id() {
    std::lock_guard lock(mu_);
    return next_elastic_id_++;
}

std::vector<int> InMemoryWorkerManager::suspended_workers() {
    std::lock_guard lock(mu_);
    std::vector<int> result;
    for (auto& [id, w] : workers_) {
        if (!w.available) result.push_back(id);
    }
    return result;
}

std::vector<int> InMemoryWorkerManager::decommissionable_workers(
    std::chrono::milliseconds threshold) {
    std::lock_guard lock(mu_);
    auto now = std::chrono::steady_clock::now();
    std::vector<int> result;
    for (auto& [id, tp] : suspended_since_) {
        if (workers_.count(id) && !workers_[id].available) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - tp);
            if (elapsed > threshold) {
                result.push_back(id);
            }
        }
    }
    return result;
}

void InMemoryWorkerManager::set_suspended_since_for_test(
    int worker_id, std::chrono::steady_clock::time_point tp) {
    std::lock_guard lock(mu_);
    suspended_since_[worker_id] = tp;
}

} // namespace labios
