#pragma once
#include <labios/label.h>
#include <labios/solver/solver.h>

#include <algorithm>

namespace labios {

/// Select or modify a weight profile based on label intent (LABIOS-SPEC S7.4).
/// Returns a copy of the base profile with weights adjusted for the intent.
inline WeightProfile profile_for_intent(const WeightProfile& base, Intent intent) {
    WeightProfile p = base;
    switch (intent) {
    case Intent::Checkpoint:
        // Checkpoint: weight capacity and speed high, tier irrelevant
        p.capacity = std::max(p.capacity, 0.4);
        p.speed = std::max(p.speed, 0.4);
        p.tier = 0.0;
        break;
    case Intent::Embedding:
    case Intent::ModelWeight:
        // Pipeline-heavy: weight tier and skills high
        p.tier = std::max(p.tier, 0.4);
        p.speed = std::max(p.speed, 0.3);
        break;
    case Intent::ReasoningTrace:
    case Intent::KVCache:
        // Complex: weight reasoning/tier high
        p.tier = std::max(p.tier, 0.5);
        break;
    case Intent::Cache:
        // Cache: weight speed and load high
        p.speed = std::max(p.speed, 0.4);
        p.load = std::max(p.load, 0.3);
        break;
    case Intent::Intermediate:
        // Intermediate: nearest available worker
        p.load = std::max(p.load, 0.2);
        p.availability = std::max(p.availability, 0.5);
        break;
    case Intent::ToolOutput:
    case Intent::FinalResult:
    case Intent::SharedState:
    case Intent::None:
    default:
        // Use base profile as-is
        break;
    }
    return p;
}

} // namespace labios
