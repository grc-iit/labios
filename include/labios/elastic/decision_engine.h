#pragma once

#include <chrono>
#include <vector>

namespace labios::elastic {

enum class Action { None, Commission, Decommission, Resume };

struct Decision {
    Action action = Action::None;
    int target_worker_id = 0;
};

struct ElasticSnapshot {
    int pressure_count;
    int pressure_threshold;
    int current_workers;
    int min_workers;
    int max_workers;
    std::vector<int> idle_worker_ids;
    std::vector<int> suspended_worker_ids;
    std::chrono::steady_clock::time_point last_commission;
    std::chrono::milliseconds cooldown;
};

/// Pure function: given the current elastic state, return the next action.
Decision evaluate(const ElasticSnapshot& state);

} // namespace labios::elastic
