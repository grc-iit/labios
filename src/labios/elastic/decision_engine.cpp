#include <labios/elastic/decision_engine.h>

#include <charconv>

namespace labios::elastic {

Decision evaluate(const ElasticSnapshot& state) {
    // Energy budget: decommission idle workers when over budget.
    if (state.energy_budget > 0.0 && state.current_energy >= state.energy_budget) {
        if (!state.idle_worker_ids.empty()) {
            return {Action::Decommission, state.idle_worker_ids.front()};
        }
        return {Action::None, 0};
    }

    // Commission / resume: sustained queue pressure with capacity available.
    if (state.pressure_count >= state.pressure_threshold &&
        state.current_workers < state.max_workers) {
        auto now = std::chrono::steady_clock::now();
        if ((now - state.last_commission) > state.cooldown) {
            if (!state.suspended_worker_ids.empty()) {
                return {Action::Resume, state.suspended_worker_ids.front()};
            }
            return {Action::Commission, 0};
        }
    }

    // Decommission: worker idle beyond threshold, no queue pressure, above minimum.
    if (!state.idle_worker_ids.empty() &&
        state.current_workers > state.min_workers &&
        state.pressure_count == 0) {
        return {Action::Decommission, state.idle_worker_ids.front()};
    }

    return {Action::None, 0};
}

// --- Tier-aware elastic scaling (Wave 8) ---

QueueBreakdown parse_queue_breakdown(const std::string& msg) {
    QueueBreakdown qb;
    // Format: "total,with_pipeline,observe_count"
    // Falls back gracefully: if only "total" is present, pipeline/observe stay 0.
    const char* p = msg.data();
    const char* end = p + msg.size();

    auto next_int = [&]() -> int {
        while (p < end && *p == ' ') ++p;
        int val = 0;
        auto [ptr, ec] = std::from_chars(p, end, val);
        p = ptr;
        if (p < end && *p == ',') ++p;
        return (ec == std::errc{}) ? val : 0;
    };

    qb.total = next_int();
    qb.with_pipeline = next_int();
    qb.observe_count = next_int();
    return qb;
}

namespace {

/// Check if a tier should commission or resume a worker.
/// Returns a ScaleDecision with Commission/Resume, or None if no action needed.
ScaleDecision try_commission(const TierState& ts, WorkerTier tier,
                             const TieredSnapshot& state,
                             const std::string& reason) {
    int total = ts.active;
    if (total >= ts.max) return {Action::None, tier, -1, {}};

    auto now = std::chrono::steady_clock::now();
    if ((now - state.last_commission) <= state.cooldown) {
        return {Action::None, tier, -1, {}};
    }

    if (!ts.suspended_ids.empty()) {
        return {Action::Resume, tier, ts.suspended_ids.front(),
                "resume " + reason};
    }
    return {Action::Commission, tier, -1, "commission " + reason};
}

/// Check if a tier has decommissionable workers above its minimum.
ScaleDecision try_decommission(const TierState& ts, WorkerTier tier,
                               const std::string& reason) {
    if (ts.idle_ids.empty() || ts.active <= ts.min) {
        return {Action::None, tier, -1, {}};
    }
    return {Action::Decommission, tier, ts.idle_ids.front(),
            "decommission idle " + reason};
}

} // namespace

ScaleDecision evaluate_tiered(const TieredSnapshot& state) {
    // Energy budget: decommission idle workers when over budget.
    if (state.energy_budget > 0.0 && state.current_energy >= state.energy_budget) {
        auto d = try_decommission(state.databot, WorkerTier::Databot, "energy budget exceeded");
        if (d.action != Action::None) return d;
        d = try_decommission(state.pipeline, WorkerTier::Pipeline, "energy budget exceeded");
        if (d.action != Action::None) return d;
        d = try_decommission(state.agentic, WorkerTier::Agentic, "energy budget exceeded");
        if (d.action != Action::None) return d;
        return {Action::None, WorkerTier::Databot, -1, "energy budget exceeded, no idle workers"};
    }

    bool under_pressure = state.pressure_count >= state.pressure_threshold;
    bool has_pipeline_demand = state.queue.with_pipeline > 0;

    // --- Commission / Resume ---
    if (under_pressure) {
        // Pipeline labels present and no pipeline+ workers available:
        // commission a Pipeline worker.
        if (has_pipeline_demand &&
            (state.pipeline.active + state.agentic.active) == 0) {
            auto d = try_commission(state.pipeline, WorkerTier::Pipeline,
                                    state, "pipeline worker for SDS labels");
            if (d.action != Action::None) return d;
        }

        // General I/O overload: commission a Databot.
        {
            auto d = try_commission(state.databot, WorkerTier::Databot,
                                    state, "databot for I/O pressure");
            if (d.action != Action::None) return d;
        }

        // If databots are maxed, try pipeline tier as overflow.
        if (has_pipeline_demand) {
            auto d = try_commission(state.pipeline, WorkerTier::Pipeline,
                                    state, "pipeline worker (overflow)");
            if (d.action != Action::None) return d;
        }
    }

    // --- Decommission: lowest tier first ---
    if (state.pressure_count == 0) {
        auto d = try_decommission(state.databot, WorkerTier::Databot, "databot");
        if (d.action != Action::None) return d;

        d = try_decommission(state.pipeline, WorkerTier::Pipeline, "pipeline");
        if (d.action != Action::None) return d;

        d = try_decommission(state.agentic, WorkerTier::Agentic, "agentic");
        if (d.action != Action::None) return d;
    }

    return {Action::None, WorkerTier::Databot, -1, {}};
}

} // namespace labios::elastic
