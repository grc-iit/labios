#pragma once

#include <labios/solver/solver.h>

#include <chrono>
#include <string>
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
    double energy_budget = 0.0;     // Max total energy (0 = unlimited)
    double current_energy = 0.0;    // Sum of active workers' energy ratings
};

/// Pure function: given the current elastic state, return the next action.
Decision evaluate(const ElasticSnapshot& state);

// --- Tier-aware elastic scaling (Wave 8) ---

/// Queue depth breakdown published by the dispatcher.
struct QueueBreakdown {
    int total = 0;
    int with_pipeline = 0;    // Labels carrying non-empty SDS pipelines
    int observe_count = 0;    // OBSERVE labels (informational)
};

/// Parse the dispatcher's queue depth message: "total,with_pipeline,observe_count"
QueueBreakdown parse_queue_breakdown(const std::string& msg);

/// Per-tier worker counts and limits.
struct TierState {
    int active = 0;
    int min = 0;
    int max = 0;
    std::vector<int> idle_ids;        // Decommissionable workers of this tier
    std::vector<int> suspended_ids;   // Suspended workers of this tier
};

struct TieredSnapshot {
    QueueBreakdown queue;
    int pressure_count;
    int pressure_threshold;
    TierState databot;
    TierState pipeline;
    TierState agentic;
    std::chrono::steady_clock::time_point last_commission;
    std::chrono::milliseconds cooldown;
    double energy_budget = 0.0;     // Max total energy (0 = unlimited)
    double current_energy = 0.0;    // Sum of active workers' energy ratings
};

struct ScaleDecision {
    Action action = Action::None;
    WorkerTier target_tier = WorkerTier::Databot;
    int target_worker_id = -1;
    std::string reason;
};

/// Tier-aware evaluation: decides which tier to commission/decommission.
ScaleDecision evaluate_tiered(const TieredSnapshot& state);

} // namespace labios::elastic
