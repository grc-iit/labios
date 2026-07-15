#pragma once
#include <labios/label.h>
#include <labios/solver/solver.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace labios {

enum class DemandKind { Exact, LowerBound, Unknown };
enum class PlacementOutcome { Assigned, Deferred };
struct ByteDemand { uint64_t bytes = 0; DemandKind kind = DemandKind::Unknown; };
struct ResourceRequirement {
    uint8_t family = 0;
    std::string backend_id;
    std::string scheme;
    std::string identity;
    std::string locality_domain;
    bool hard_locality = false;
};
struct JobDescriptor {
    uint64_t unit_id = 0;
    uint64_t label_id = 0;
    uint64_t ordinal = 0;
    LabelType type = LabelType::Write;
    std::string operation;
    uint32_t operation_version = 1;
    uint32_t ir_version = kCurrentIrVersion;
    WorkerTier minimum_tier = WorkerTier::Databot;
    std::vector<std::string> pipeline_operations;
    std::vector<uint32_t> pipeline_operation_versions;
    std::vector<ResourceRequirement> sources, destinations;
    ByteDemand demand;
    Intent intent = Intent::None;
    uint8_t priority = 0;
    bool ready = true;
};
struct SchedulingUnitDescriptor {
    uint64_t unit_id = 0;
    uint64_t ordinal = 0;
    std::vector<JobDescriptor> members;
    bool ready = true;
    std::vector<uint64_t> predecessors;
};
struct SchedulingBatch { uint64_t batch_id = 0; uint64_t registry_generation = 0; std::vector<SchedulingUnitDescriptor> units; };

enum class FeasibilityReason {
    None, Invalid, Unavailable, UnsupportedIr, UnsupportedOperation,
    InsufficientTier, MissingPipelineOperation, MissingBackendAttachment,
    HardLocalityMismatch, InsufficientSingleJobCapacity, ExhaustedBatchCapacity,
    NoWorkers, NoAvailableWorker, NoFeasibleCurrentPlacement
};
const char* feasibility_reason_name(FeasibilityReason);
struct FeasibilityResult {
    bool feasible = false;
    std::vector<FeasibilityReason> reasons;
    bool locality_match = false;
};
struct FeasibilityMatrix { std::vector<std::vector<FeasibilityResult>> values; };
struct PreparedSchedulingBatch { SchedulingBatch batch; std::vector<WorkerInfo> workers; FeasibilityMatrix matrix; std::vector<uint64_t> budgets; };
struct PlacementDecision {
    uint64_t unit_id = 0;
    PlacementOutcome outcome = PlacementOutcome::Deferred;
    int worker_id = -1;
    FeasibilityReason deferred_reason = FeasibilityReason::None;
    std::string park_reason;
    std::string evidence;
    std::vector<CandidateEvaluation> candidates;
};
struct PlacementPlan { std::vector<PlacementDecision> decisions; };

std::optional<JobDescriptor> describe_job(const LabelData&, uint64_t ordinal=0);
PreparedSchedulingBatch prepare_scheduling_batch(SchedulingBatch, std::vector<WorkerInfo>);
bool validate_plan(const PreparedSchedulingBatch&, const PlacementPlan&);
PlacementPlan solve_prepared(const PreparedSchedulingBatch&, std::string_view policy, const WeightProfile& = {});
void append_decision_history(LabelData&, const SchedulingDecisionSnapshot&);

} // namespace labios
