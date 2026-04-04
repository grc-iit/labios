#include <labios/elastic/decision_engine.h>

namespace labios::elastic {

Decision evaluate(const ElasticSnapshot& state) {
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

} // namespace labios::elastic
