#include <labios/scheduling/scheduling.h>
#include <labios/solver/intent_profiles.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <random>
#include <sstream>
#include <tuple>
#include <unordered_set>

namespace labios {
namespace {

std::string canonical_operation(const LabelData& label) {
    if (!label.operation.empty()) {
        if (label.operation == "read") return "core.read";
        if (label.operation == "write") return "core.write";
        return label.operation;
    }
    switch (label.type) {
    case LabelType::Read: return "core.read";
    case LabelType::Write: return "core.write";
    case LabelType::Delete: return "core.delete";
    case LabelType::Flush: return "core.flush";
    case LabelType::Observe: return "core.observe";
    case LabelType::Composite: return "core.composite";
    }
    return "";
}

std::string resource_identity(const ResourceRef& resource) {
    if (!resource.logical_id.empty()) return resource.logical_id;
    switch (resource.family) {
    case ResourceFamily::FileRange: return resource.path;
    case ResourceFamily::Relational: return resource.database + "/" + resource.schema + "/" + resource.key;
    case ResourceFamily::KeyValue: return resource.database + "/" + resource.namespace_name + "/" + resource.key;
    case ResourceFamily::Object: return resource.bucket + "/" + resource.key;
    case ResourceFamily::Memory: return resource.owner + "/" + resource.allocation_id;
    default: return resource.path.empty() ? resource.key : resource.path;
    }
}

std::string family_scheme(ResourceFamily family) {
    switch (family) {
    case ResourceFamily::FileRange: return "file";
    case ResourceFamily::Relational: return "sqlite";
    case ResourceFamily::KeyValue: return "kv";
    case ResourceFamily::Object: return "object";
    case ResourceFamily::Memory: return "memory";
    default: return {};
    }
}

ResourceRequirement requirement(const ResourceRef& resource) {
    ResourceRequirement out;
    out.family = static_cast<uint8_t>(resource.family);
    out.backend_id = resource.backend_id;
    out.scheme = family_scheme(resource.family);
    out.identity = resource_identity(resource);
    return out;
}

bool supports_versioned(const std::vector<std::string>& names,
                        const std::vector<uint32_t>& versions,
                        const std::string& name, uint32_t version) {
    if (names.size() != versions.size()) return false;
    for (size_t index = 0; index < names.size(); ++index) {
        if (names[index] == name && versions[index] == version) return true;
    }
    return false;
}

void add_reason(std::vector<FeasibilityReason>& reasons, FeasibilityReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

bool locality_matches(const ResourceRequirement& requirement, const WorkerInfo& worker) {
    if (requirement.locality_domain.empty()) return false;
    if (std::find(worker.locality_domains.begin(), worker.locality_domains.end(),
                  requirement.locality_domain) != worker.locality_domains.end()) return true;
    return std::any_of(worker.attachments.begin(), worker.attachments.end(),
        [&](const WorkerAttachment& attachment) {
            return attachment.locality_domain == requirement.locality_domain;
        });
}

bool attachment_matches(const WorkerAttachment& attachment,
                        const ResourceRequirement& requirement) {
    if (attachment.family != requirement.family) return false;
    if (!requirement.backend_id.empty() && attachment.backend_id != requirement.backend_id) return false;
    if (!requirement.scheme.empty() &&
        attachment.scheme != requirement.scheme) return false;
    return true;
}

bool worker_has_attachment(const WorkerInfo& worker, const ResourceRequirement& requirement) {
    return std::any_of(worker.attachments.begin(), worker.attachments.end(),
                       [&](const WorkerAttachment& attachment) {
                           return attachment_matches(attachment, requirement);
                       });
}

uint64_t unit_demand(const SchedulingUnitDescriptor& unit, bool& overflow, bool& known) {
    uint64_t total = 0;
    known = true;
    for (const auto& job : unit.members) {
        if (job.demand.kind != DemandKind::Exact) known = false;
        if (job.demand.kind == DemandKind::Unknown) continue;
        if (std::numeric_limits<uint64_t>::max() - total < job.demand.bytes) {
            overflow = true;
            return 0;
        }
        total += job.demand.bytes;
    }
    return total;
}

FeasibilityResult evaluate(const SchedulingUnitDescriptor& unit,
                           const WorkerInfo& worker) {
    FeasibilityResult result;
    if (!worker.available) add_reason(result.reasons, FeasibilityReason::Unavailable);
    for (const auto& job : unit.members) {
        if (worker.max_ir_version < job.ir_version) {
            add_reason(result.reasons, FeasibilityReason::UnsupportedIr);
        }
        if (!supports_versioned(worker.operations, worker.operation_versions,
                                job.operation, job.operation_version)) {
            add_reason(result.reasons, FeasibilityReason::UnsupportedOperation);
        }
        if (static_cast<uint8_t>(worker.tier) < static_cast<uint8_t>(job.minimum_tier)) {
            add_reason(result.reasons, FeasibilityReason::InsufficientTier);
        }
        for (size_t index = 0; index < job.pipeline_operations.size(); ++index) {
            const auto version = index < job.pipeline_operation_versions.size()
                ? job.pipeline_operation_versions[index] : 0U;
            if (!supports_versioned(worker.pipeline_operations,
                                    worker.pipeline_operation_versions,
                                    job.pipeline_operations[index], version)) {
                add_reason(result.reasons, FeasibilityReason::MissingPipelineOperation);
            }
        }
        for (const auto& source : job.sources) {
            if (!worker_has_attachment(worker, source)) {
                add_reason(result.reasons, FeasibilityReason::MissingBackendAttachment);
            }
            if (source.hard_locality && !locality_matches(source, worker)) {
                add_reason(result.reasons, FeasibilityReason::HardLocalityMismatch);
            }
            result.locality_match = result.locality_match || locality_matches(source, worker);
        }
        for (const auto& destination : job.destinations) {
            if (!worker_has_attachment(worker, destination)) {
                add_reason(result.reasons, FeasibilityReason::MissingBackendAttachment);
            }
            if (destination.hard_locality && !locality_matches(destination, worker)) {
                add_reason(result.reasons, FeasibilityReason::HardLocalityMismatch);
            }
            result.locality_match = result.locality_match || locality_matches(destination, worker);
        }
    }
    result.feasible = result.reasons.empty();
    return result;
}

double remaining_ratio(const WorkerInfo& worker, uint64_t remaining) {
    if (worker.total_capacity_bytes != 0) {
        return std::min(1.0, static_cast<double>(remaining) /
                                  static_cast<double>(worker.total_capacity_bytes));
    }
    return std::clamp(worker.capacity, 0.0, 1.0);
}

double profit(const WorkerInfo& worker, uint64_t remaining) {
    const auto capacity = remaining_ratio(worker, remaining);
    const auto energy = std::max(static_cast<double>(worker.energy) / 5.0, 0.01);
    return (static_cast<double>(worker.speed) / 5.0) * capacity *
           (1.0 - std::clamp(worker.load, 0.0, 1.0)) / energy;
}

bool trace_guided(const WeightProfile& profile) {
    return profile.trace_service != 0.0 || profile.trace_queue != 0.0 ||
           profile.trace_throughput != 0.0;
}

std::string trace_scheme(const SchedulingUnitDescriptor& unit) {
    if (unit.members.empty()) return {};
    const auto& job = unit.members.front();
    if (!job.destinations.empty() && !job.destinations.front().scheme.empty()) {
        return job.destinations.front().scheme;
    }
    if (!job.sources.empty()) return job.sources.front().scheme;
    return {};
}

double scheme_throughput(const WorkerInfo& worker, std::string_view scheme) {
    if (!scheme.empty()) {
        const auto it = worker.trace_scheme_throughput.find(std::string(scheme));
        if (it != worker.trace_scheme_throughput.end()) return it->second;
    }
    return worker.trace_throughput_bytes_per_sec;
}

std::atomic_int rr_cursor_worker_id{-1};
}

const char* feasibility_reason_name(FeasibilityReason reason) {
    switch (reason) {
    case FeasibilityReason::Unavailable: return "UNAVAILABLE";
    case FeasibilityReason::UnsupportedIr: return "UNSUPPORTED_IR";
    case FeasibilityReason::UnsupportedOperation: return "UNSUPPORTED_OPERATION";
    case FeasibilityReason::InsufficientTier: return "INSUFFICIENT_TIER";
    case FeasibilityReason::MissingPipelineOperation: return "MISSING_PIPELINE_OPERATION";
    case FeasibilityReason::MissingBackendAttachment: return "MISSING_BACKEND_ATTACHMENT";
    case FeasibilityReason::HardLocalityMismatch: return "HARD_LOCALITY_MISMATCH";
    case FeasibilityReason::InsufficientSingleJobCapacity: return "INSUFFICIENT_SINGLE_JOB_CAPACITY";
    case FeasibilityReason::ExhaustedBatchCapacity: return "EXHAUSTED_BATCH_CAPACITY";
    case FeasibilityReason::BlockedDependency: return "BLOCKED_DEPENDENCY";
    case FeasibilityReason::NoWorkers: return "NO_WORKERS";
    case FeasibilityReason::NoAvailableWorker: return "NO_AVAILABLE_WORKER";
    case FeasibilityReason::NoFeasibleCurrentPlacement: return "NO_FEASIBLE_CURRENT_PLACEMENT";
    case FeasibilityReason::Invalid: return "INVALID";
    case FeasibilityReason::None: return "NONE";
    }
    return "INVALID";
}

std::optional<JobDescriptor> describe_job(const LabelData& label, uint64_t ordinal) {
    JobDescriptor job;
    job.unit_id = label.id;
    job.label_id = label.id;
    job.ordinal = ordinal;
    job.type = label.type;
    job.operation = canonical_operation(label);
    job.operation_version = label.operation_version == 0 ? 1 : label.operation_version;
    job.ir_version = label.ir_version == 0 ? kCurrentIrVersion : label.ir_version;
    job.intent = label.intent;
    job.priority = label.priority;
    // Memory resources used for staged producer input and completion retrieval
    // are internal data bindings, not external backend attachments. Requiring a
    // worker to advertise them would permanently park ordinary SDK reads/writes.
    if (label.has_source_resource &&
        label.source_resource.family != ResourceFamily::Memory) {
        job.sources.push_back(requirement(label.source_resource));
    }
    if (label.has_destination_resource &&
        label.destination_resource.family != ResourceFamily::Memory) {
        job.destinations.push_back(requirement(label.destination_resource));
    }
    if (!label.pipeline.stages.empty()) {
        job.minimum_tier = WorkerTier::Pipeline;
        for (const auto& stage : label.pipeline.stages) {
            job.pipeline_operations.push_back(stage.operation);
            job.pipeline_operation_versions.push_back(1);
        }
    }
    if (label.type == LabelType::Delete || label.type == LabelType::Flush ||
        label.type == LabelType::Observe) {
        job.demand = {0, DemandKind::Exact};
    } else if (label.data_size != 0) {
        job.demand = {label.data_size, DemandKind::Exact};
    } else if (label.has_input_binding && label.input_binding.logical_length != 0) {
        job.demand = {label.input_binding.logical_length, DemandKind::LowerBound};
    } else {
        job.demand = {0, DemandKind::Unknown};
    }
    return job;
}

PreparedSchedulingBatch prepare_scheduling_batch(SchedulingBatch batch,
                                                   std::vector<WorkerInfo> workers) {
    std::sort(workers.begin(), workers.end(), [](const WorkerInfo& left, const WorkerInfo& right) {
        return left.id < right.id;
    });
    PreparedSchedulingBatch prepared{std::move(batch), std::move(workers), {}, {}};
    prepared.matrix.values.resize(prepared.batch.units.size(),
                                  std::vector<FeasibilityResult>(prepared.workers.size()));
    prepared.budgets.reserve(prepared.workers.size());
    for (const auto& worker : prepared.workers) {
        prepared.budgets.push_back(worker.available_capacity_bytes);
    }
    for (size_t unit_index = 0; unit_index < prepared.batch.units.size(); ++unit_index) {
        bool overflow = false;
        bool known = false;
        const auto bytes = unit_demand(prepared.batch.units[unit_index], overflow, known);
        for (size_t worker_index = 0; worker_index < prepared.workers.size(); ++worker_index) {
            auto& evaluation = prepared.matrix.values[unit_index][worker_index] =
                evaluate(prepared.batch.units[unit_index], prepared.workers[worker_index]);
            if (!prepared.batch.units[unit_index].ready) {
                add_reason(evaluation.reasons,
                           FeasibilityReason::BlockedDependency);
            }
            if (prepared.batch.units[unit_index].members.empty()) {
                add_reason(evaluation.reasons, FeasibilityReason::Invalid);
            }
            if (overflow) add_reason(evaluation.reasons, FeasibilityReason::Invalid);
            if (!overflow && bytes != 0 &&
                bytes > prepared.workers[worker_index].available_capacity_bytes) {
                add_reason(evaluation.reasons, FeasibilityReason::InsufficientSingleJobCapacity);
            }
            evaluation.feasible = evaluation.reasons.empty();
        }
    }
    return prepared;
}

PlacementPlan solve_prepared(const PreparedSchedulingBatch& prepared,
                              std::string_view policy,
                              const WeightProfile& profile) {
    PlacementPlan plan;
    plan.decisions.resize(prepared.batch.units.size());
    std::vector<uint64_t> remaining = prepared.budgets;
    const auto seed = prepared.batch.batch_id == 0 ? 1ULL : prepared.batch.batch_id;
    std::mt19937_64 random(seed);
    std::vector<size_t> ids(prepared.workers.size());
    for (size_t index = 0; index < ids.size(); ++index) ids[index] = index;

    size_t cursor = 0;
    const auto cursor_worker_id = rr_cursor_worker_id.load();
    if (!ids.empty() && cursor_worker_id >= 0) {
        const auto it = std::lower_bound(
            prepared.workers.begin(), prepared.workers.end(), cursor_worker_id,
            [](const WorkerInfo& worker, int id) { return worker.id < id; });
        cursor = it == prepared.workers.end()
            ? 0 : static_cast<size_t>(it - prepared.workers.begin());
    }

    bool minmax_all_reservations_known = true;
    bool minmax_overflow = false;
    uint64_t minmax_total_bytes = 0;
    uint64_t minmax_ready_units = 0;
    for (const auto& unit : prepared.batch.units) {
        if (!unit.ready) continue;
        ++minmax_ready_units;
        bool overflow = false;
        bool complete_known = false;
        const auto bytes = unit_demand(unit, overflow, complete_known);
        minmax_overflow = minmax_overflow || overflow;
        const auto has_unknown = std::any_of(
            unit.members.begin(), unit.members.end(), [](const auto& job) {
                return job.demand.kind == DemandKind::Unknown;
            });
        minmax_all_reservations_known =
            minmax_all_reservations_known && !has_unknown;
        if (!overflow &&
            std::numeric_limits<uint64_t>::max() - minmax_total_bytes >= bytes) {
            minmax_total_bytes += bytes;
        } else {
            minmax_overflow = true;
        }
    }
    if (minmax_overflow) minmax_all_reservations_known = false;
    const auto minmax_basis = minmax_all_reservations_known
        ? SchedulingDemandBasis::ReservationBytes
        : SchedulingDemandBasis::UnitCount;
    const uint64_t minmax_total_demand = minmax_all_reservations_known
        ? minmax_total_bytes : minmax_ready_units;
    std::vector<double> minmax_profit(prepared.workers.size(), 0.0);
    std::vector<double> minmax_share(prepared.workers.size(), 0.0);
    std::vector<double> minmax_target(prepared.workers.size(), 0.0);
    std::vector<double> minmax_consumed(prepared.workers.size(), 0.0);
    std::vector<bool> minmax_relevant(prepared.workers.size(), false);
    std::vector<bool> minmax_cold_explored(prepared.workers.size(), false);
    double minmax_profit_sum = 0.0;
    for (size_t worker_index = 0; worker_index < prepared.workers.size();
         ++worker_index) {
        for (size_t unit_index = 0; unit_index < prepared.batch.units.size();
             ++unit_index) {
            if (prepared.batch.units[unit_index].ready &&
                prepared.matrix.values[unit_index][worker_index].feasible) {
                minmax_relevant[worker_index] = true;
                break;
            }
        }
        if (minmax_relevant[worker_index]) {
            minmax_profit[worker_index] =
                profit(prepared.workers[worker_index],
                       prepared.budgets[worker_index]);
            minmax_profit_sum += minmax_profit[worker_index];
        }
    }
    const auto relevant_count = static_cast<double>(std::count(
        minmax_relevant.begin(), minmax_relevant.end(), true));
    for (size_t worker_index = 0; worker_index < prepared.workers.size();
         ++worker_index) {
        if (!minmax_relevant[worker_index]) continue;
        minmax_share[worker_index] = minmax_profit_sum > 0.0
            ? minmax_profit[worker_index] / minmax_profit_sum
            : 1.0 / std::max(1.0, relevant_count);
        minmax_target[worker_index] =
            minmax_share[worker_index] *
            static_cast<double>(minmax_total_demand);
    }
    double minmax_batch_objective = 0.0;

    for (size_t unit_index = 0; unit_index < prepared.batch.units.size(); ++unit_index) {
        const auto& unit = prepared.batch.units[unit_index];
        auto& decision = plan.decisions[unit_index];
        decision.unit_id = unit.unit_id;
        bool overflow = false;
        bool complete_known = false;
        const auto bytes = unit_demand(unit, overflow, complete_known);
        std::vector<size_t> feasible;
        for (size_t worker_index = 0; worker_index < prepared.workers.size(); ++worker_index) {
            auto evaluation = prepared.matrix.values[unit_index][worker_index];
            if (evaluation.feasible && (bytes == 0 || bytes <= remaining[worker_index])) {
                feasible.push_back(worker_index);
            }
        }

        if (!unit.ready) {
            decision.deferred_reason = FeasibilityReason::NoFeasibleCurrentPlacement;
            decision.park_reason = "BLOCKED_PREDECESSOR";
        } else if (prepared.workers.empty()) {
            decision.deferred_reason = FeasibilityReason::NoWorkers;
            decision.park_reason = feasibility_reason_name(decision.deferred_reason);
        } else if (std::none_of(prepared.workers.begin(), prepared.workers.end(),
                               [](const WorkerInfo& worker) { return worker.available; })) {
            decision.deferred_reason = FeasibilityReason::NoAvailableWorker;
            decision.park_reason = feasibility_reason_name(decision.deferred_reason);
        } else if (feasible.empty()) {
            decision.deferred_reason = FeasibilityReason::NoFeasibleCurrentPlacement;
            decision.park_reason = feasibility_reason_name(decision.deferred_reason);
        }

        // Candidate rows are always emitted in ascending worker-ID order. The
        // matrix is sorted by ID by prepare_scheduling_batch.
        for (size_t worker_index = 0; worker_index < prepared.workers.size(); ++worker_index) {
            const auto& worker = prepared.workers[worker_index];
            const auto& matrix = prepared.matrix.values[unit_index][worker_index];
            CandidateEvaluation candidate;
            candidate.worker_id = worker.id;
            candidate.feasible = matrix.feasible &&
                (bytes == 0 || bytes <= remaining[worker_index]);
            candidate.reason_codes.reserve(matrix.reasons.size() + 1);
            for (const auto reason : matrix.reasons) {
                candidate.reason_codes.emplace_back(feasibility_reason_name(reason));
            }
            if (matrix.feasible && bytes != 0 && bytes > remaining[worker_index]) {
                candidate.feasible = false;
                candidate.reason_codes.emplace_back(
                    feasibility_reason_name(FeasibilityReason::ExhaustedBatchCapacity));
            }
            candidate.available_capacity_before = remaining[worker_index];
            candidate.available_capacity_after = remaining[worker_index];
            candidate.locality_match = matrix.locality_match;
            decision.candidates.push_back(std::move(candidate));
        }

        if (decision.deferred_reason != FeasibilityReason::None) {
            decision.evidence = decision.park_reason;
            if (policy == "random") {
                decision.structured_policy_kind = StructuredPolicyKind::Random;
                decision.random.batch_seed = seed;
                decision.tie_break = "ascending-worker-id-candidate-order";
            } else if (policy == "constraint") {
                decision.structured_policy_kind =
                    StructuredPolicyKind::Constraint;
                decision.constraint.profile_name =
                    profile.name.empty() ? "default" : profile.name;
                decision.tie_break = "soft-locality,ascending-worker-id";
            } else if (policy == "minmax") {
                decision.structured_policy_kind = StructuredPolicyKind::MinMax;
                decision.minmax.demand_basis = minmax_basis;
                decision.minmax.total_demand = minmax_total_demand;
                decision.minmax.profile_name = profile.name;
                decision.minmax.final_batch_objective =
                    minmax_batch_objective;
                for (size_t worker_index = 0;
                     worker_index < prepared.workers.size(); ++worker_index) {
                    decision.minmax.workers.push_back({
                        prepared.workers[worker_index].id,
                        minmax_profit[worker_index],
                        minmax_share[worker_index],
                        minmax_target[worker_index],
                        minmax_consumed[worker_index],
                        minmax_consumed[worker_index], 0});
                }
                decision.tie_break = "soft-locality,ascending-worker-id";
            } else {
                decision.structured_policy_kind =
                    StructuredPolicyKind::RoundRobin;
                decision.round_robin.cursor_worker_id_before =
                    ids.empty() ? -1 : prepared.workers[cursor].id;
                decision.tie_break =
                    "circular-ascending-worker-id";
            }
            continue;
        }

        size_t selected = feasible.front();
        if (policy == "random") {
            const auto count = static_cast<uint64_t>(feasible.size());
            const auto rejection_limit =
                std::numeric_limits<uint64_t>::max() -
                (std::numeric_limits<uint64_t>::max() % count);
            uint64_t draw = 0;
            do {
                draw = random();
            } while (draw >= rejection_limit);
            const auto selected_index =
                static_cast<size_t>(draw % feasible.size());
            selected = feasible[selected_index];
            decision.structured_policy_kind = StructuredPolicyKind::Random;
            decision.random = {seed, draw,
                               static_cast<uint32_t>(feasible.size()),
                               static_cast<uint32_t>(selected_index)};
            decision.tie_break = "ascending-worker-id-candidate-order";
            for (size_t rank = 0; rank < feasible.size(); ++rank) {
                decision.candidates[feasible[rank]].policy_rank =
                    static_cast<uint32_t>(rank + 1);
            }
            std::ostringstream evidence;
            evidence << "seed=" << seed << ";draw=" << draw
                     << ";candidate_count=" << feasible.size()
                     << ";selected_index=" << selected_index;
            decision.evidence = evidence.str();
        } else if (policy == "constraint") {
            const auto job_profile = unit.members.empty() ? profile
                : profile_for_intent(profile, unit.members.front().intent);
            const bool informed = trace_guided(job_profile);
            const auto scheme = trace_scheme(unit);
            double fastest_trace = std::numeric_limits<double>::max();
            double highest_throughput = 0.0;
            for (const auto worker_index : feasible) {
                const auto& worker = prepared.workers[worker_index];
                if (worker.trace_samples >= job_profile.trace_min_samples &&
                    worker.trace_service_us > 0.0)
                    fastest_trace = std::min(fastest_trace, worker.trace_service_us);
                highest_throughput = std::max(highest_throughput,
                                               scheme_throughput(worker, scheme));
            }
            const bool has_service_trace = fastest_trace != std::numeric_limits<double>::max();
            auto trace_values = [&](const WorkerInfo& worker) {
                const auto sampled =
                    worker.trace_samples >= job_profile.trace_min_samples;
                const auto service = informed && sampled && has_service_trace &&
                                     worker.trace_service_us > 0.0
                    ? std::clamp(fastest_trace / worker.trace_service_us, 0.0, 1.0)
                    : job_profile.trace_cold_start;
                const auto queue = informed && sampled
                    ? 1.0 - std::clamp(
                          worker.trace_queue_depth /
                              job_profile.trace_queue_anchor,
                          0.0, 1.0)
                    : job_profile.trace_cold_start;
                const auto throughput =
                    informed && sampled && highest_throughput > 0.0
                    ? std::clamp(scheme_throughput(worker, scheme) /
                                     highest_throughput,
                                 0.0, 1.0)
                    : job_profile.trace_cold_start;
                return std::array<double, 3>{service, queue, throughput};
            };
            auto score = [&](size_t worker_index) {
                const auto& worker = prepared.workers[worker_index];
                const auto availability = 1.0;
                const auto capacity = remaining_ratio(worker, remaining[worker_index]);
                const auto inverse_load = 1.0 - std::clamp(worker.load, 0.0, 1.0);
                const auto speed = std::clamp(static_cast<double>(worker.speed) / 5.0, 0.0, 1.0);
                const auto energy = 1.0 - std::clamp((static_cast<double>(worker.energy) - 1.0) / 4.0, 0.0, 1.0);
                const auto traces = trace_values(worker);
                return job_profile.availability * availability + job_profile.capacity * capacity +
                       job_profile.load * inverse_load + job_profile.speed * speed +
                       job_profile.energy * energy + job_profile.tier *
                       (static_cast<double>(static_cast<uint8_t>(worker.tier)) / 2.0) +
                       job_profile.skills * worker.skills + job_profile.compute * worker.compute +
                       job_profile.reasoning * (static_cast<double>(worker.reasoning) / 5.0) +
                       job_profile.trace_service * traces[0] +
                       job_profile.trace_queue * traces[1] +
                       job_profile.trace_throughput * traces[2];
            };
            std::sort(feasible.begin(), feasible.end(),
                      [&](size_t left, size_t right) {
                const auto left_score = score(left);
                const auto right_score = score(right);
                if (left_score != right_score) return left_score > right_score;
                const auto left_local =
                    prepared.matrix.values[unit_index][left].locality_match;
                const auto right_local =
                    prepared.matrix.values[unit_index][right].locality_match;
                if (left_local != right_local) return left_local;
                return prepared.workers[left].id <
                       prepared.workers[right].id;
            });
            selected = feasible.front();
            decision.structured_policy_kind =
                StructuredPolicyKind::Constraint;
            decision.constraint.profile_name =
                job_profile.name.empty() ? "default" : job_profile.name;
            decision.constraint.profile_version = 1;
            decision.tie_break = "soft-locality,ascending-worker-id";
            for (size_t rank = 0; rank < feasible.size(); ++rank) {
                decision.candidates[feasible[rank]].policy_rank =
                    static_cast<uint32_t>(rank + 1);
            }
            std::ostringstream evidence;
            evidence << "profile=" << (job_profile.name.empty() ? "default" : job_profile.name)
                     << ";version=1;trace=" << (informed ? "enabled" : "disabled")
                     << ";scheme=" << scheme
                     << ";trace_weights=" << job_profile.trace_service << ","
                     << job_profile.trace_queue << ","
                     << job_profile.trace_throughput
                     << ";trace_min_samples=" << job_profile.trace_min_samples;
            decision.evidence = evidence.str();
            for (auto& candidate : decision.candidates) {
                const auto worker_it = std::find_if(prepared.workers.begin(), prepared.workers.end(),
                    [&](const WorkerInfo& worker) { return worker.id == candidate.worker_id; });
                if (worker_it == prepared.workers.end()) continue;
                const auto& worker = *worker_it;
                const auto cap = remaining_ratio(worker, candidate.available_capacity_before);
                const auto traces = trace_values(worker);
                const auto throughput_raw = scheme_throughput(worker, scheme);
                const std::array<std::tuple<std::string, double, double, double>, 12> metrics{{
                    {"availability", worker.available ? 1.0 : 0.0, 1.0, job_profile.availability},
                    {"capacity", static_cast<double>(candidate.available_capacity_before), cap, job_profile.capacity},
                    {"inverse_load", worker.load, 1.0 - std::clamp(worker.load, 0.0, 1.0), job_profile.load},
                    {"speed", static_cast<double>(worker.speed), static_cast<double>(worker.speed) / 5.0, job_profile.speed},
                    {"inverse_energy", static_cast<double>(worker.energy), 1.0 - std::clamp((static_cast<double>(worker.energy) - 1.0) / 4.0, 0.0, 1.0), job_profile.energy},
                    {"tier", static_cast<double>(static_cast<uint8_t>(worker.tier)), static_cast<double>(static_cast<uint8_t>(worker.tier)) / 2.0, job_profile.tier},
                    {"skills", worker.skills, worker.skills, job_profile.skills},
                    {"compute", worker.compute, worker.compute, job_profile.compute},
                    {"reasoning", static_cast<double>(worker.reasoning), static_cast<double>(worker.reasoning) / 5.0, job_profile.reasoning},
                    {"trace_service_time", worker.trace_service_us, traces[0], job_profile.trace_service},
                    {"trace_queue_depth", worker.trace_queue_depth, traces[1], job_profile.trace_queue},
                    {"trace_throughput", throughput_raw, traces[2], job_profile.trace_throughput}
                }};
                double objective = 0.0;
                for (const auto& [name, raw, normalized, weight] : metrics) {
                    const auto contribution = normalized * weight;
                    objective += contribution;
                    candidate.score_components.push_back(
                        {name, raw, normalized, weight, contribution});
                }
                candidate.final_objective = objective;
                candidate.trace_sample_count = worker.trace_samples;
                candidate.trace_service_anchor =
                    has_service_trace ? fastest_trace : 0.0;
                candidate.trace_queue_anchor = job_profile.trace_queue_anchor;
                candidate.trace_throughput_anchor = highest_throughput;
            }
        } else if (policy == "minmax") {
            const bool informed = trace_guided(profile);
            const auto scheme = trace_scheme(unit);
            double fastest_trace = std::numeric_limits<double>::max();
            double highest_throughput = 0.0;
            for (const auto worker_index : feasible) {
                const auto& worker = prepared.workers[worker_index];
                if (worker.trace_samples >= profile.trace_min_samples &&
                    worker.trace_service_us > 0.0)
                    fastest_trace = std::min(fastest_trace, worker.trace_service_us);
                highest_throughput = std::max(highest_throughput,
                                               scheme_throughput(worker, scheme));
            }
            auto objective = [&](size_t worker_index) {
                const auto& worker = prepared.workers[worker_index];
                const auto base = profit(worker, remaining[worker_index]);
                if (!informed) return base;
                const auto sampled =
                    worker.trace_samples >= profile.trace_min_samples;
                const auto service = sampled &&
                                             fastest_trace !=
                                                 std::numeric_limits<double>::max() &&
                                             worker.trace_service_us > 0.0
                    ? std::clamp(fastest_trace / worker.trace_service_us, 0.0, 1.0)
                    : profile.trace_cold_start;
                const auto queue = sampled
                    ? 1.0 - std::clamp(
                          worker.trace_queue_depth / profile.trace_queue_anchor,
                          0.0, 1.0)
                    : profile.trace_cold_start;
                const auto throughput = sampled && highest_throughput > 0.0
                    ? std::clamp(scheme_throughput(worker, scheme) /
                                     highest_throughput,
                                 0.0, 1.0)
                    : profile.trace_cold_start;
                return base * (1.0 + profile.trace_service * service +
                               profile.trace_queue * queue +
                               profile.trace_throughput * throughput);
            };
            std::sort(feasible.begin(), feasible.end(),
                      [&](size_t left, size_t right) {
                const auto left_profit = objective(left);
                const auto right_profit = objective(right);
                if (left_profit != right_profit)
                    return left_profit > right_profit;
                const auto left_local =
                    prepared.matrix.values[unit_index][left].locality_match;
                const auto right_local =
                    prepared.matrix.values[unit_index][right].locality_match;
                if (left_local != right_local) return left_local;
                return prepared.workers[left].id <
                       prepared.workers[right].id;
            });
            for (size_t rank = 0; rank < feasible.size(); ++rank) {
                decision.candidates[feasible[rank]].policy_rank =
                    static_cast<uint32_t>(rank + 1);
            }
            const auto cold = std::find_if(
                feasible.begin(), feasible.end(), [&](size_t worker_index) {
                    return informed &&
                           prepared.workers[worker_index].trace_samples <
                               profile.trace_min_samples &&
                           !minmax_cold_explored[worker_index];
                });
            uint32_t spill_rank = 0;
            bool cold_exploration = false;
            if (cold != feasible.end()) {
                selected = *cold;
                minmax_cold_explored[selected] = true;
                cold_exploration = true;
                spill_rank =
                    static_cast<uint32_t>(cold - feasible.begin());
            } else {
                const auto amount = minmax_basis ==
                        SchedulingDemandBasis::ReservationBytes
                    ? static_cast<double>(bytes)
                    : 1.0;
                const auto target_candidate = std::find_if(
                    feasible.begin(), feasible.end(), [&](size_t worker_index) {
                        return minmax_consumed[worker_index] + amount <=
                               minmax_target[worker_index] + 1e-12;
                    });
                if (target_candidate != feasible.end()) {
                    selected = *target_candidate;
                    spill_rank = static_cast<uint32_t>(
                        target_candidate - feasible.begin());
                } else {
                    selected = *std::max_element(
                        feasible.begin(), feasible.end(),
                        [&](size_t left, size_t right) {
                            const auto left_deficit =
                                minmax_target[left] -
                                minmax_consumed[left];
                            const auto right_deficit =
                                minmax_target[right] -
                                minmax_consumed[right];
                            if (left_deficit != right_deficit)
                                return left_deficit < right_deficit;
                            const auto left_objective = objective(left);
                            const auto right_objective = objective(right);
                            if (left_objective != right_objective)
                                return left_objective < right_objective;
                            const auto left_local = prepared.matrix
                                .values[unit_index][left].locality_match;
                            const auto right_local = prepared.matrix
                                .values[unit_index][right].locality_match;
                            if (left_local != right_local)
                                return !left_local;
                            return prepared.workers[left].id >
                                   prepared.workers[right].id;
                        });
                    spill_rank = static_cast<uint32_t>(
                        std::find(feasible.begin(), feasible.end(), selected) -
                        feasible.begin());
                }
            }
            const auto amount = minmax_basis ==
                    SchedulingDemandBasis::ReservationBytes
                ? static_cast<double>(bytes)
                : 1.0;
            decision.structured_policy_kind = StructuredPolicyKind::MinMax;
            decision.minmax.demand_basis = minmax_basis;
            decision.minmax.total_demand = minmax_total_demand;
            decision.minmax.profile_name = profile.name;
            decision.minmax.profile_version = 1;
            decision.minmax.cold_exploration = cold_exploration;
            decision.tie_break = cold_exploration
                ? "bounded-cold-exploration,soft-locality,ascending-worker-id"
                : "target-share,soft-locality,ascending-worker-id";
            for (size_t worker_index = 0;
                 worker_index < prepared.workers.size(); ++worker_index) {
                decision.minmax.workers.push_back({
                    prepared.workers[worker_index].id,
                    objective(worker_index),
                    minmax_share[worker_index],
                    minmax_target[worker_index],
                    minmax_consumed[worker_index],
                    minmax_consumed[worker_index] +
                        (worker_index == selected ? amount : 0.0),
                    worker_index == selected ? spill_rank : 0U});
            }
            minmax_consumed[selected] += amount;
            minmax_batch_objective += objective(selected) * amount;
            decision.minmax.final_batch_objective = minmax_batch_objective;
            std::ostringstream evidence;
            evidence << "basis=" << (minmax_basis ==
                        SchedulingDemandBasis::ReservationBytes
                            ? "bytes" : "unit-count")
                     << ";spill_rank=" << spill_rank
                     << ";objective=" << objective(selected)
                     << ";trace=" << (informed ? "enabled" : "disabled")
                     << ";scheme=" << scheme
                     << ";profile=" << profile.name
                     << ";trace_weights=" << profile.trace_service << ","
                     << profile.trace_queue << ","
                     << profile.trace_throughput;
            decision.evidence = evidence.str();
            for (auto& candidate : decision.candidates) {
                const auto worker_it = std::find_if(prepared.workers.begin(), prepared.workers.end(),
                    [&](const WorkerInfo& worker) { return worker.id == candidate.worker_id; });
                if (worker_it != prepared.workers.end()) {
                    candidate.final_objective =
                        objective(static_cast<size_t>(worker_it - prepared.workers.begin()));
                    candidate.trace_sample_count = worker_it->trace_samples;
                    candidate.trace_service_anchor =
                        fastest_trace == std::numeric_limits<double>::max()
                            ? 0.0 : fastest_trace;
                    candidate.trace_queue_anchor = profile.trace_queue_anchor;
                    candidate.trace_throughput_anchor = highest_throughput;
                    if (informed) {
                        const auto& worker = *worker_it;
                        const auto sampled =
                            worker.trace_samples >= profile.trace_min_samples;
                        const auto service = sampled &&
                                                     fastest_trace !=
                                                         std::numeric_limits<double>::max() &&
                                                     worker.trace_service_us > 0.0
                            ? std::clamp(fastest_trace / worker.trace_service_us,
                                         0.0, 1.0)
                            : profile.trace_cold_start;
                        const auto queue = sampled
                            ? 1.0 - std::clamp(
                                  worker.trace_queue_depth /
                                      profile.trace_queue_anchor,
                                  0.0, 1.0)
                            : profile.trace_cold_start;
                        const auto throughput =
                            sampled && highest_throughput > 0.0
                            ? std::clamp(
                                  scheme_throughput(worker, scheme) /
                                      highest_throughput,
                                  0.0, 1.0)
                            : profile.trace_cold_start;
                        candidate.score_components.push_back(
                            {"trace_service_time", worker.trace_service_us, service,
                             profile.trace_service,
                             service * profile.trace_service});
                        candidate.score_components.push_back(
                            {"trace_queue_depth", worker.trace_queue_depth, queue,
                             profile.trace_queue, queue * profile.trace_queue});
                        candidate.score_components.push_back(
                            {"trace_throughput",
                             scheme_throughput(worker, scheme), throughput,
                             profile.trace_throughput,
                             throughput * profile.trace_throughput});
                    }
                }
            }
        } else {
            const auto before = cursor;
            size_t rank = 0;
            for (size_t offset = 0; offset < ids.size(); ++offset) {
                const auto candidate = ids[(cursor + offset) % ids.size()];
                if (std::find(feasible.begin(), feasible.end(), candidate) != feasible.end()) {
                    selected = candidate;
                    rank = offset;
                    cursor = (candidate + 1) % ids.size();
                    break;
                }
            }
            rr_cursor_worker_id.store(
                ids.empty() ? -1 : prepared.workers[cursor].id);
            decision.structured_policy_kind =
                StructuredPolicyKind::RoundRobin;
            decision.round_robin.cursor_worker_id_before =
                ids.empty() ? -1 : prepared.workers[before].id;
            decision.round_robin.selected_scan_rank =
                static_cast<uint32_t>(rank);
            decision.tie_break = "circular-ascending-worker-id";
            for (size_t offset = 0; offset < ids.size(); ++offset) {
                const auto candidate = ids[(before + offset) % ids.size()];
                decision.candidates[candidate].policy_rank =
                    static_cast<uint32_t>(offset + 1);
            }
            std::ostringstream evidence;
            evidence << "cursor_worker_id_before="
                     << decision.round_robin.cursor_worker_id_before
                     << ";scan_rank=" << rank;
            decision.evidence = evidence.str();
        }

        decision.outcome = PlacementOutcome::Assigned;
        decision.worker_id = prepared.workers[selected].id;
        for (auto& candidate : decision.candidates) {
            if (candidate.worker_id == decision.worker_id) {
                candidate.selected = true;
                candidate.available_capacity_after =
                    bytes <= candidate.available_capacity_before
                        ? candidate.available_capacity_before - bytes
                        : candidate.available_capacity_before;
            }
        }
        if (bytes != 0) remaining[selected] -= bytes;
    }
    return plan;
}

bool validate_plan(const PreparedSchedulingBatch& prepared, const PlacementPlan& plan) {
    if (plan.decisions.size() != prepared.batch.units.size()) return false;
    std::vector<uint64_t> remaining = prepared.budgets;
    std::unordered_set<uint64_t> seen;
    for (size_t index = 0; index < plan.decisions.size(); ++index) {
        const auto& decision = plan.decisions[index];
        if (decision.unit_id != prepared.batch.units[index].unit_id ||
            !seen.insert(decision.unit_id).second) return false;
        if (decision.outcome != PlacementOutcome::Assigned) {
            if (decision.deferred_reason == FeasibilityReason::None ||
                decision.park_reason.empty()) return false;
            continue;
        }
        if (!prepared.batch.units[index].ready ||
            prepared.batch.units[index].members.empty()) return false;
        const auto worker_it = std::find_if(prepared.workers.begin(), prepared.workers.end(),
            [&](const WorkerInfo& worker) { return worker.id == decision.worker_id; });
        if (worker_it == prepared.workers.end()) return false;
        const auto worker_index = static_cast<size_t>(worker_it - prepared.workers.begin());
        if (!prepared.matrix.values[index][worker_index].feasible) return false;
        bool overflow = false;
        bool known = false;
        const auto bytes = unit_demand(prepared.batch.units[index], overflow, known);
        if (overflow || (bytes != 0 && bytes > remaining[worker_index])) return false;
        if (bytes != 0) remaining[worker_index] -= bytes;
    }
    return true;
}

void append_decision_history(LabelData& label, const SchedulingDecisionSnapshot& decision) {
    label.score_snapshot.decision_version = 1;
    label.score_snapshot.decisions.push_back(decision);
}

SchedulingDecisionSnapshot make_decision_snapshot(
    const PreparedSchedulingBatch& prepared, size_t unit_index,
    const PlacementDecision& decision, uint32_t attempt,
    std::string_view policy, const WeightProfile& profile) {
    SchedulingDecisionSnapshot history;
    if (unit_index >= prepared.batch.units.size()) return history;
    const auto& unit = prepared.batch.units[unit_index];
    history.decision_id =
        (prepared.batch.batch_id << 1U) ^ unit.unit_id ^
        static_cast<uint64_t>(attempt);
    history.batch_id = prepared.batch.batch_id;
    history.scheduling_unit_id = unit.unit_id;
    history.attempt = attempt;
    history.registry_generation = prepared.batch.registry_generation;
    history.job_ordinal = unit.ordinal;
    history.outcome = decision.outcome == PlacementOutcome::Assigned
        ? "Assigned" : "Parked";
    history.chosen_worker_id = decision.worker_id;
    history.park_reason = decision.outcome == PlacementOutcome::Assigned
        ? std::string{}
        : (decision.park_reason.empty()
               ? feasibility_reason_name(decision.deferred_reason)
               : decision.park_reason);
    bool overflow = false;
    bool complete_known = false;
    history.reservation_bytes = unit_demand(unit, overflow, complete_known);
    if (overflow) {
        history.reservation_bytes = 0;
        complete_known = false;
    }
    history.complete_size_known = complete_known;
    history.candidates = decision.candidates;
    history.policy_name = std::string(policy);
    history.policy_evidence = decision.evidence;
    history.policy_version = decision.policy_version;
    history.tie_break = decision.tie_break;
    history.structured_policy_kind = decision.structured_policy_kind;
    history.round_robin = decision.round_robin;
    history.random = decision.random;
    history.constraint = decision.constraint;
    history.minmax = decision.minmax;
    history.replay_unit.unit_id = unit.unit_id;
    history.replay_unit.ordinal = unit.ordinal;
    history.replay_unit.ready = unit.ready;
    history.replay_unit.predecessors = unit.predecessors;
    for (const auto& member : unit.members) {
        ReplayJobDescriptor job;
        job.unit_id = member.unit_id;
        job.label_id = member.label_id;
        job.ordinal = member.ordinal;
        job.type = member.type;
        job.operation = member.operation;
        job.operation_version = member.operation_version;
        job.ir_version = member.ir_version;
        job.minimum_tier =
            static_cast<uint8_t>(member.minimum_tier);
        job.pipeline_operations = member.pipeline_operations;
        job.pipeline_operation_versions =
            member.pipeline_operation_versions;
        auto copy_resources = [](const auto& source, auto& destination) {
            for (const auto& value : source) {
                destination.push_back({
                    value.family, value.backend_id, value.scheme,
                    value.identity, value.locality_domain,
                    value.hard_locality});
            }
        };
        copy_resources(member.sources, job.sources);
        copy_resources(member.destinations, job.destinations);
        job.demand_bytes = member.demand.bytes;
        job.demand_kind = static_cast<uint8_t>(member.demand.kind);
        job.intent = member.intent;
        job.priority = member.priority;
        job.ready = member.ready;
        history.replay_unit.members.push_back(std::move(job));
    }
    for (const auto& worker : prepared.workers) {
        ReplayWorkerSnapshot snapshot;
        snapshot.worker_id = worker.id;
        snapshot.available = worker.available;
        snapshot.capacity = worker.capacity;
        snapshot.load = worker.load;
        snapshot.speed = worker.speed;
        snapshot.energy = worker.energy;
        snapshot.tier = static_cast<uint8_t>(worker.tier);
        snapshot.skills = worker.skills;
        snapshot.compute = worker.compute;
        snapshot.reasoning = worker.reasoning;
        snapshot.registration_epoch = worker.registration_epoch;
        snapshot.total_capacity_bytes = worker.total_capacity_bytes;
        snapshot.available_capacity_bytes =
            worker.available_capacity_bytes;
        snapshot.max_ir_version = worker.max_ir_version;
        snapshot.operations = worker.operations;
        snapshot.operation_versions = worker.operation_versions;
        snapshot.pipeline_operations = worker.pipeline_operations;
        snapshot.pipeline_operation_versions =
            worker.pipeline_operation_versions;
        for (const auto& attachment : worker.attachments) {
            snapshot.attachments.push_back({
                attachment.family, attachment.backend_id,
                attachment.scheme,
                static_cast<uint8_t>(attachment.locality),
                attachment.locality_domain});
        }
        snapshot.locality_domains = worker.locality_domains;
        snapshot.trace_samples = worker.trace_samples;
        snapshot.trace_service_us = worker.trace_service_us;
        snapshot.trace_queue_depth = worker.trace_queue_depth;
        snapshot.trace_throughput_bytes_per_sec =
            worker.trace_throughput_bytes_per_sec;
        for (const auto& [scheme, throughput] :
             worker.trace_scheme_throughput) {
            snapshot.trace_scheme_throughput.push_back(
                {scheme, throughput});
        }
        std::sort(snapshot.trace_scheme_throughput.begin(),
                  snapshot.trace_scheme_throughput.end(),
                  [](const auto& left, const auto& right) {
                      return left.scheme < right.scheme;
                  });
        history.replay_workers.push_back(std::move(snapshot));
    }
    history.replay_profile = {
        profile.name, profile.availability, profile.capacity,
        profile.load, profile.speed, profile.energy, profile.tier,
        profile.skills, profile.compute, profile.reasoning,
        profile.trace_service, profile.trace_queue,
        profile.trace_throughput, profile.trace_cold_start,
        profile.trace_queue_anchor, profile.trace_min_samples};
    return history;
}

void apply_scheduling_decision(LabelData& label,
                               const SchedulingDecisionSnapshot& history,
                               const PlacementDecision& decision,
                               const WorkerInfo* worker,
                               std::string_view policy,
                               uint64_t dispatched_us) {
    if (decision.outcome == PlacementOutcome::Assigned && worker != nullptr) {
        ScoreSnapshot snapshot;
        snapshot.availability = worker->available ? 1.0 : 0.0;
        snapshot.capacity = worker->capacity;
        snapshot.load = worker->load;
        snapshot.speed = worker->speed;
        snapshot.energy = worker->energy;
        snapshot.tier = static_cast<double>(
            static_cast<uint8_t>(worker->tier));
        snapshot.decision_version = 1;
        snapshot.decisions = label.score_snapshot.decisions;
        snapshot.decisions.push_back(history);
        mark_label_scheduled(label, static_cast<uint32_t>(worker->id),
                             std::string(policy), snapshot, dispatched_us);
        return;
    }
    append_decision_history(label, history);
    label.status = StatusCode::Queued;
}

PreparedSchedulingBatch reconstruct_prepared_scheduling_batch(
    const std::vector<SchedulingDecisionSnapshot>& history) {
    SchedulingBatch batch;
    if (history.empty()) return prepare_scheduling_batch({}, {});
    batch.batch_id = history.front().batch_id;
    batch.registry_generation = history.front().registry_generation;
    std::vector<const SchedulingDecisionSnapshot*> ordered;
    ordered.reserve(history.size());
    for (const auto& decision : history) ordered.push_back(&decision);
    std::sort(ordered.begin(), ordered.end(), [](const auto* left,
                                                  const auto* right) {
        return left->replay_unit.ordinal < right->replay_unit.ordinal;
    });
    for (const auto* decision : ordered) {
        SchedulingUnitDescriptor unit;
        unit.unit_id = decision->replay_unit.unit_id;
        unit.ordinal = decision->replay_unit.ordinal;
        unit.ready = decision->replay_unit.ready;
        unit.predecessors = decision->replay_unit.predecessors;
        for (const auto& replay : decision->replay_unit.members) {
            JobDescriptor job;
            job.unit_id = replay.unit_id;
            job.label_id = replay.label_id;
            job.ordinal = replay.ordinal;
            job.type = replay.type;
            job.operation = replay.operation;
            job.operation_version = replay.operation_version;
            job.ir_version = replay.ir_version;
            job.minimum_tier =
                static_cast<WorkerTier>(replay.minimum_tier);
            job.pipeline_operations = replay.pipeline_operations;
            job.pipeline_operation_versions =
                replay.pipeline_operation_versions;
            auto copy_resources = [](const auto& source,
                                     auto& destination) {
                for (const auto& value : source) {
                    destination.push_back({
                        value.family, value.backend_id, value.scheme,
                        value.identity, value.locality_domain,
                        value.hard_locality});
                }
            };
            copy_resources(replay.sources, job.sources);
            copy_resources(replay.destinations, job.destinations);
            job.demand = {replay.demand_bytes,
                          static_cast<DemandKind>(replay.demand_kind)};
            job.intent = replay.intent;
            job.priority = replay.priority;
            job.ready = replay.ready;
            unit.members.push_back(std::move(job));
        }
        batch.units.push_back(std::move(unit));
    }
    std::vector<WorkerInfo> workers;
    for (const auto& replay : history.front().replay_workers) {
        WorkerInfo worker;
        worker.id = replay.worker_id;
        worker.available = replay.available;
        worker.capacity = replay.capacity;
        worker.load = replay.load;
        worker.speed = replay.speed;
        worker.energy = replay.energy;
        worker.tier = static_cast<WorkerTier>(replay.tier);
        worker.skills = replay.skills;
        worker.compute = replay.compute;
        worker.reasoning = replay.reasoning;
        worker.registration_epoch = replay.registration_epoch;
        worker.total_capacity_bytes = replay.total_capacity_bytes;
        worker.available_capacity_bytes =
            replay.available_capacity_bytes;
        worker.max_ir_version = replay.max_ir_version;
        worker.operations = replay.operations;
        worker.operation_versions = replay.operation_versions;
        worker.pipeline_operations = replay.pipeline_operations;
        worker.pipeline_operation_versions =
            replay.pipeline_operation_versions;
        for (const auto& attachment : replay.attachments) {
            worker.attachments.push_back({
                attachment.family, attachment.backend_id,
                attachment.scheme,
                static_cast<LocalityKind>(attachment.locality_kind),
                attachment.locality_domain});
        }
        worker.locality_domains = replay.locality_domains;
        worker.trace_samples = replay.trace_samples;
        worker.trace_service_us = replay.trace_service_us;
        worker.trace_queue_depth = replay.trace_queue_depth;
        worker.trace_throughput_bytes_per_sec =
            replay.trace_throughput_bytes_per_sec;
        for (const auto& trace : replay.trace_scheme_throughput) {
            worker.trace_scheme_throughput[trace.scheme] =
                trace.throughput;
        }
        workers.push_back(std::move(worker));
    }
    return prepare_scheduling_batch(std::move(batch),
                                    std::move(workers));
}

WeightProfile reconstruct_weight_profile(
    const SchedulingDecisionSnapshot& decision) {
    const auto& replay = decision.replay_profile;
    WeightProfile profile;
    profile.name = replay.name;
    profile.availability = replay.availability;
    profile.capacity = replay.capacity;
    profile.load = replay.load;
    profile.speed = replay.speed;
    profile.energy = replay.energy;
    profile.tier = replay.tier;
    profile.skills = replay.skills;
    profile.compute = replay.compute;
    profile.reasoning = replay.reasoning;
    profile.trace_service = replay.trace_service;
    profile.trace_queue = replay.trace_queue;
    profile.trace_throughput = replay.trace_throughput;
    profile.trace_cold_start = replay.trace_cold_start;
    profile.trace_queue_anchor = replay.trace_queue_anchor;
    profile.trace_min_samples = replay.trace_min_samples;
    return profile;
}

} // namespace labios
