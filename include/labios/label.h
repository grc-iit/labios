#pragma once

#include <labios/sds/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <variant>
#include <vector>

namespace labios {

inline constexpr uint32_t kCurrentIrVersion = 1;

class LabelDecodeError : public std::runtime_error {
public:
    LabelDecodeError(std::string category, std::string detail);
    const std::string& category() const noexcept { return category_; }
private:
    std::string category_;
};

enum class LabelType : uint8_t { Read, Write, Delete, Flush, Composite, Observe };
enum class Intent : uint8_t {
    None, Checkpoint, Cache, ToolOutput, FinalResult,
    Intermediate, SharedState,
    Embedding, ModelWeight, KVCache, ReasoningTrace
};
enum class Isolation : uint8_t { None, Agent, Workspace, Global };
enum class HazardType : uint8_t { RAW, WAW, WAR, Order, Barrier };
enum class ResourceFamily : uint8_t { FileRange, Memory, Network, KeyValue, Relational, Object, Vector, Graph, Channel, Workspace, Extension };
enum class BindingProvenance : uint8_t { DirectProducer, MaterializedSource };
enum class CompletionStatus : uint8_t { Complete, Error };
enum class Durability : uint8_t { Ephemeral, Durable };
enum class StatusCode : uint8_t { Created, Queued, Shuffled, Scheduled, Executing, Complete, Failed };
enum class ContinuationKind : uint8_t { None, Notify, Chain, Conditional };

namespace LabelFlags {
    constexpr uint32_t Queued      = 1 << 0;
    constexpr uint32_t Scheduled   = 1 << 1;
    constexpr uint32_t Pending     = 1 << 2;
    constexpr uint32_t Cached      = 1 << 3;
    constexpr uint32_t Invalidated = 1 << 4;
    constexpr uint32_t Async       = 1 << 5;
    constexpr uint32_t HighPrio    = 1 << 6;
} // namespace LabelFlags

struct MemoryPtr {
    uint64_t address;
    uint64_t size;
};

struct FilePath {
    std::string path;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct NetworkEndpoint {
    std::string host;
    uint16_t port;
};

using Pointer = std::variant<std::monostate, MemoryPtr, FilePath, NetworkEndpoint>;

struct ResourceRef {
    ResourceFamily family = ResourceFamily::FileRange;
    std::string backend_id;
    std::string logical_id;
    std::string namespace_name;
    std::string database;
    std::string schema;
    std::string path;
    std::string key;
    std::string selector;
    std::string host;
    std::string transport;
    std::string stream;
    std::string bucket;
    std::string collection;
    std::string item_id;
    std::string graph;
    std::string element_id;
    std::string owner;
    std::string allocation_id;
    std::string transfer_token;
    std::string extent = "Unspecified";
    uint64_t offset = 0;
    uint64_t length = 0;
    uint16_t port = 0;
    uint32_t schema_version = 0;
    std::string version_token;
    bool version_exact = false;
    bool version_must_not_exist = false;
    friend bool operator==(const ResourceRef&, const ResourceRef&) = default;
};

struct StagedInputBinding {
    BindingProvenance provenance = BindingProvenance::DirectProducer;
    std::string content_id;
    uint64_t logical_length = 0;
    std::string digest_algorithm;
    std::vector<std::byte> digest;
    std::string observed_version;
};

ResourceRef resource_from_uri(std::string_view uri);
ResourceRef resource_from_pointer(const Pointer& pointer, uint64_t label_id = 0,
                                  uint32_t app_id = 0);

Pointer memory_ptr(const void* addr, uint64_t size);
Pointer file_path(std::string_view path);
Pointer file_path(std::string_view path, uint64_t offset, uint64_t length);
Pointer network_endpoint(std::string_view host, uint16_t port);

struct LabelDependency {
    uint64_t label_id = 0;
    HazardType hazard_type = HazardType::RAW;
};

struct Continuation {
    ContinuationKind kind = ContinuationKind::None;
    std::string target_channel;
    std::string chain_params;
    std::string condition;
};

struct RoutingDecision {
    uint32_t worker_id = 0;
    std::string policy;
};

struct HopRecord {
    std::string component;
    uint64_t timestamp_us = 0;
};

struct AggregationInfo {
    std::vector<uint64_t> original_ids;   // IDs of merged labels
    uint64_t merged_offset = 0;
    uint64_t merged_length = 0;
};

struct ScoreComponent {
    std::string metric;
    double raw_value = 0.0;
    double normalized_value = 0.0;
    double weight = 0.0;
    double contribution = 0.0;
};

struct CandidateEvaluation {
    int worker_id = 0;
    bool feasible = false;
    std::vector<std::string> reason_codes;
    uint64_t available_capacity_before = 0;
    uint64_t available_capacity_after = 0;
    bool locality_match = false;
    std::vector<ScoreComponent> score_components;
    double final_objective = 0.0;
    uint32_t policy_rank = 0;
    bool selected = false;
    uint64_t trace_sample_count = 0;
    double trace_service_anchor = 0.0;
    double trace_queue_anchor = 0.0;
    double trace_throughput_anchor = 0.0;
};

enum class SchedulingDemandBasis : uint8_t {
    Unknown = 0,
    ReservationBytes = 1,
    UnitCount = 2,
};

enum class StructuredPolicyKind : uint8_t {
    None = 0,
    RoundRobin = 1,
    Random = 2,
    Constraint = 3,
    MinMax = 4,
};

struct RoundRobinPolicyEvidence {
    int cursor_worker_id_before = -1;
    uint32_t selected_scan_rank = 0;
};

struct RandomPolicyEvidence {
    uint64_t batch_seed = 0;
    uint64_t raw_draw = 0;
    uint32_t candidate_count = 0;
    uint32_t selected_index = 0;
};

struct ConstraintPolicyEvidence {
    std::string profile_name;
    uint32_t profile_version = 1;
};

struct MinMaxWorkerEvidence {
    int worker_id = 0;
    double profit = 0.0;
    double target_share = 0.0;
    double target_amount = 0.0;
    double consumption_before = 0.0;
    double consumption_after = 0.0;
    uint32_t spill_rank = 0;
};

struct MinMaxPolicyEvidence {
    SchedulingDemandBasis demand_basis = SchedulingDemandBasis::Unknown;
    uint64_t total_demand = 0;
    double final_batch_objective = 0.0;
    std::vector<MinMaxWorkerEvidence> workers;
    std::string profile_name;
    uint32_t profile_version = 1;
    bool cold_exploration = false;
};

struct ReplayResourceRequirement {
    uint8_t family = 0;
    std::string backend_id;
    std::string scheme;
    std::string identity;
    std::string locality_domain;
    bool hard_locality = false;
};

struct ReplayJobDescriptor {
    uint64_t unit_id = 0;
    uint64_t label_id = 0;
    uint64_t ordinal = 0;
    LabelType type = LabelType::Write;
    std::string operation;
    uint32_t operation_version = 1;
    uint32_t ir_version = kCurrentIrVersion;
    uint8_t minimum_tier = 0;
    std::vector<std::string> pipeline_operations;
    std::vector<uint32_t> pipeline_operation_versions;
    std::vector<ReplayResourceRequirement> sources;
    std::vector<ReplayResourceRequirement> destinations;
    uint64_t demand_bytes = 0;
    uint8_t demand_kind = 2;
    Intent intent = Intent::None;
    uint8_t priority = 0;
    bool ready = true;
};

struct ReplaySchedulingUnit {
    uint64_t unit_id = 0;
    uint64_t ordinal = 0;
    std::vector<ReplayJobDescriptor> members;
    bool ready = true;
    std::vector<uint64_t> predecessors;
};

struct ReplayWorkerAttachment {
    uint8_t family = 0;
    std::string backend_id;
    std::string scheme;
    uint8_t locality_kind = 0;
    std::string locality_domain;
};

struct ReplayTraceScheme {
    std::string scheme;
    double throughput = 0.0;
};

struct ReplayWorkerSnapshot {
    int worker_id = 0;
    bool available = true;
    double capacity = 1.0;
    double load = 0.0;
    int speed = 1;
    int energy = 1;
    uint8_t tier = 0;
    double skills = 0.0;
    double compute = 1.0;
    int reasoning = 0;
    uint64_t registration_epoch = 1;
    uint64_t total_capacity_bytes = 0;
    uint64_t available_capacity_bytes = 0;
    uint32_t max_ir_version = 1;
    std::vector<std::string> operations;
    std::vector<uint32_t> operation_versions;
    std::vector<std::string> pipeline_operations;
    std::vector<uint32_t> pipeline_operation_versions;
    std::vector<ReplayWorkerAttachment> attachments;
    std::vector<std::string> locality_domains;
    uint64_t trace_samples = 0;
    double trace_service_us = 0.0;
    double trace_queue_depth = 0.0;
    double trace_throughput_bytes_per_sec = 0.0;
    std::vector<ReplayTraceScheme> trace_scheme_throughput;
};

struct ReplayWeightProfile {
    std::string name;
    double availability = 0.0;
    double capacity = 0.0;
    double load = 0.0;
    double speed = 0.0;
    double energy = 0.0;
    double tier = 0.0;
    double skills = 0.0;
    double compute = 0.0;
    double reasoning = 0.0;
    double trace_service = 0.0;
    double trace_queue = 0.0;
    double trace_throughput = 0.0;
    double trace_cold_start = 0.5;
    double trace_queue_anchor = 1.0;
    uint64_t trace_min_samples = 1;
};

struct SchedulingDecisionSnapshot {
    uint64_t decision_id = 0;
    uint64_t batch_id = 0;
    uint64_t scheduling_unit_id = 0;
    uint32_t attempt = 0;
    uint64_t registry_generation = 0;
    uint64_t job_ordinal = 0;
    std::string outcome; // Assigned or Parked
    int chosen_worker_id = -1;
    std::string park_reason;
    uint64_t reservation_bytes = 0;
    bool complete_size_known = false;
    std::vector<CandidateEvaluation> candidates;
    std::string policy_name;
    std::string policy_evidence;
    uint32_t policy_version = 1;
    std::string tie_break;
    StructuredPolicyKind structured_policy_kind = StructuredPolicyKind::None;
    RoundRobinPolicyEvidence round_robin;
    RandomPolicyEvidence random;
    ConstraintPolicyEvidence constraint;
    MinMaxPolicyEvidence minmax;
    ReplaySchedulingUnit replay_unit;
    std::vector<ReplayWorkerSnapshot> replay_workers;
    ReplayWeightProfile replay_profile;
};

struct ScoreSnapshot {
    double availability = 0.0;
    double capacity = 0.0;
    double load = 0.0;
    double speed = 0.0;
    double energy = 0.0;
    double tier = 0.0;
    uint32_t decision_version = 0;
    std::vector<SchedulingDecisionSnapshot> decisions;
};

struct LabelResult {
    std::string data_location;
    std::string error;
    uint64_t bytes_transferred = 0;
};

struct LabelData {
    uint32_t ir_version = kCurrentIrVersion;
    uint64_t id = 0;
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    uint32_t app_id = 0;
    std::vector<LabelDependency> dependencies;
    std::vector<uint64_t> declared_dependencies;
    ResourceRef source_resource;
    ResourceRef destination_resource;
    bool has_source_resource = false;
    bool has_destination_resource = false;
    StagedInputBinding input_binding;
    bool has_input_binding = false;
    uint32_t operation_version = 1;
    uint64_t data_size = 0;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;
    std::string reply_to;
    std::string file_key;              // Normalized path for shuffler grouping
    std::vector<uint64_t> children;    // Supertask child label IDs

    // Spec fields
    uint64_t version = 0;
    Durability durability = Durability::Ephemeral;
    Continuation continuation;
    std::string source_uri;
    std::string dest_uri;
    sds::Pipeline pipeline;

    // Accumulation (written by runtime)
    RoutingDecision routing;
    uint64_t supertask_id = 0;           // Set by shuffler if label joins supertask
    AggregationInfo aggregation;          // Set by shuffler on merged labels
    ScoreSnapshot score_snapshot;         // Set by scheduler at decision time
    std::vector<HopRecord> hops;

    // State
    StatusCode status = StatusCode::Created;
    uint64_t created_us = 0;
    uint64_t queued_us = 0;              // When label entered dispatch queue
    uint64_t dispatched_us = 0;          // When scheduler assigned to worker
    uint64_t started_us = 0;             // When worker began execution
    uint64_t completed_us = 0;
    LabelResult result;                   // Execution outcome
};

struct LabelParams {
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    std::vector<LabelDependency> dependencies;
    std::vector<uint64_t> declared_dependencies;
    Intent intent = Intent::None;
    ResourceRef source_resource;
    ResourceRef destination_resource;
    bool has_source_resource = false;
    bool has_destination_resource = false;
    StagedInputBinding input_binding;
    bool has_input_binding = false;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;

    // Spec additions (Wave 10)
    uint64_t version = 0;
    Durability durability = Durability::Ephemeral;
    Continuation continuation;
    std::string source_uri;
    std::string dest_uri;
    sds::Pipeline pipeline;
};

struct CompletionData {
    uint64_t label_id = 0;
    CompletionStatus status = CompletionStatus::Complete;
    std::string error;
    std::string data_key;

    // Append-only, worker-observed execution measurement. Version 1 defines
    // queue delay as queued_us -> started_us and service time as
    // started_us -> completed_us for this scheduling attempt.
    uint32_t observation_version = 0;
    int worker_id = -1;
    uint32_t attempt = 0;
    uint64_t queued_us = 0;
    uint64_t dispatched_us = 0;
    uint64_t started_us = 0;
    uint64_t completed_us = 0;
    uint64_t queue_delay_us = 0;
    uint64_t service_time_us = 0;
};

uint64_t generate_label_id(uint32_t app_id);
uint64_t label_timestamp_now_us();

void append_label_hop(LabelData& label, std::string_view component,
                      uint64_t timestamp_us = 0);
void mark_label_created(LabelData& label, uint64_t timestamp_us = 0);
void mark_label_queued(LabelData& label, uint64_t timestamp_us = 0);
void mark_label_shuffled(LabelData& label, uint64_t timestamp_us = 0);
void mark_label_scheduled(LabelData& label, uint32_t worker_id,
                          std::string_view policy,
                          const ScoreSnapshot& snapshot,
                          uint64_t timestamp_us = 0);
void mark_label_executing(LabelData& label, std::string_view worker_component,
                          uint64_t timestamp_us = 0);
/// Retain the newest runtime decision records without allowing retry history
/// to grow with parking cycles. Attempt accounting remains catalog-owned.
void bound_decision_history(LabelData& label, size_t max_records = 32);

void mark_label_finished(LabelData& label, CompletionStatus status,
                         std::string_view data_location = {},
                         uint64_t bytes_transferred = 0,
                         std::string_view error = {},
                         uint64_t timestamp_us = 0);
/// Copy coherent worker execution timestamps into the completion's public
/// measurement surface. Incoherent or incomplete timestamps leave the
/// observation absent rather than fabricating a duration.
void record_completion_observation(CompletionData& completion,
                                   const LabelData& label, int worker_id);

std::vector<std::byte> serialize_label(const LabelData& label);

/// Zero-copy variant: returns a span into a thread-local buffer.
/// Valid only until the next serialize call on the same thread.
std::span<const std::byte> serialize_label_view(const LabelData& label);

LabelData deserialize_label(std::span<const std::byte> buf);

/// Validate a normalized producer label before admission.
/// Throws LabelDecodeError with a stable CATEGORY: detail message.
void validate_label_admission(const LabelData& label);
void normalize_label_resources(LabelData& label);

std::vector<std::byte> serialize_completion(const CompletionData& completion);
CompletionData deserialize_completion(std::span<const std::byte> buf);

} // namespace labios
