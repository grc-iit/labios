#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace labios {

/// Worker tiers from LABIOS-SPEC (Section 3).
/// Tier 0: Stateless databot, single I/O ops only.
/// Tier 1: Pipeline worker, executes SDS function DAGs.
/// Tier 2: Agentic worker, reasoning-capable with tool use.
enum class WorkerTier : uint8_t { Databot = 0, Pipeline = 1, Agentic = 2 };

/// Worker score variables from the paper (Section 3.2.3, Table 2).
/// The composite score is: sum(weight_j * variable_j) for j=1..6.
struct WorkerInfo {
    int id;
    bool available = true;

    // Dynamic variables (updated continuously by workers)
    double capacity = 1.0;       // [0,1] remaining/total capacity
    double load = 0.0;           // [0,1] queue_size/max_queue_size

    // Static variables (set at initialization)
    int speed = 1;               // [1,5] bandwidth class of storage medium
    int energy = 1;              // [1,5] power wattage class
    WorkerTier tier = WorkerTier::Databot;

    // 2026 extension variables (LABIOS-SPEC S7.3)
    double skills = 0.0;          // [0,1] fraction of requested capabilities
    double compute = 1.0;         // [0,1] available CPU/memory resources
    int reasoning = 0;            // [0,5] model capability class (0=none)

    // Composite score computed by the worker manager
    double score = 1.0;          // [0,1] weighted combination
};

/// Weight profile for computing worker composite scores (Table 2).
struct WeightProfile {
    std::string name;
    double availability = 0.0;
    double capacity = 0.0;
    double load = 0.0;
    double speed = 0.0;
    double energy = 0.0;
    double tier = 0.0;

    // 2026 extension variables
    double skills = 0.0;
    double compute = 0.0;
    double reasoning = 0.0;
};

using AssignmentMap = std::unordered_map<int, std::vector<std::vector<std::byte>>>;

/// Solver concept: takes labels and workers, returns assignment map.
template<typename T>
concept Solver = requires(T s,
    std::vector<std::vector<std::byte>> labels,
    std::vector<WorkerInfo> workers) {
    { s.assign(std::move(labels), std::move(workers)) } -> std::same_as<AssignmentMap>;
};

/// WorkerManager concept: maintains worker registry and scores.
/// The dispatcher queries the manager for ranked workers.
template<typename T>
concept WorkerManager = requires(T m, int n, const WeightProfile& wp) {
    { m.all_workers() } -> std::same_as<std::vector<WorkerInfo>>;
    { m.top_n_workers(n, wp) } -> std::same_as<std::vector<WorkerInfo>>;
    { m.update_score(int{}, WorkerInfo{}) };
    { m.register_worker(WorkerInfo{}) };
    { m.deregister_worker(int{}) };
};

} // namespace labios
