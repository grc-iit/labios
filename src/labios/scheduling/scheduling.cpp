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

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return values.empty() || std::find(values.begin(), values.end(), value) != values.end() ||
           std::find(values.begin(), values.end(), "*") != values.end();
}

void add_reason(std::vector<FeasibilityReason>& reasons, FeasibilityReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

bool locality_matches(const ResourceRequirement& requirement, const WorkerInfo& worker) {
    if (requirement.locality_domain.empty()) return true;
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
    if (!requirement.scheme.empty() && !attachment.scheme.empty() &&
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
        if (job.demand.kind == DemandKind::Unknown) known = false;
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
        if (!contains(worker.operations, job.operation)) {
            add_reason(result.reasons, FeasibilityReason::UnsupportedOperation);
        }
        if (static_cast<uint8_t>(worker.tier) < static_cast<uint8_t>(job.minimum_tier)) {
            add_reason(result.reasons, FeasibilityReason::InsufficientTier);
        }
        for (const auto& operation : job.pipeline_operations) {
            if (worker.pipeline_operations.empty() || !contains(worker.pipeline_operations, operation)) {
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

std::atomic_size_t rr_cursor{0};
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
    if (label.has_source_resource) job.sources.push_back(requirement(label.source_resource));
    if (label.has_destination_resource) job.destinations.push_back(requirement(label.destination_resource));
    if (!label.pipeline.stages.empty()) {
        job.minimum_tier = WorkerTier::Pipeline;
        for (const auto& stage : label.pipeline.stages) {
            job.pipeline_operations.push_back(stage.operation);
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
            if (overflow) add_reason(evaluation.reasons, FeasibilityReason::Invalid);
            if (!overflow && bytes != 0 &&
                prepared.workers[worker_index].available_capacity_bytes != 0 &&
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
    plan.decisions.reserve(prepared.batch.units.size());
    std::vector<uint64_t> remaining = prepared.budgets;
    const auto seed = prepared.batch.batch_id == 0 ? 1ULL : prepared.batch.batch_id;
    std::mt19937_64 random(seed);
    std::vector<size_t> ids(prepared.workers.size());
    for (size_t index = 0; index < ids.size(); ++index) ids[index] = index;
    size_t cursor = ids.empty() ? 0 : rr_cursor.load();
    if (!ids.empty()) cursor %= ids.size();

    for (size_t unit_index = 0; unit_index < prepared.batch.units.size(); ++unit_index) {
        const auto& unit = prepared.batch.units[unit_index];
        PlacementDecision decision;
        decision.unit_id = unit.unit_id;
        bool overflow = false;
        bool known = false;
        const auto bytes = unit_demand(unit, overflow, known);
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
            candidate.available_capacity_after =
                candidate.feasible && bytes <= remaining[worker_index]
                    ? remaining[worker_index] - bytes : remaining[worker_index];
            candidate.locality_match = matrix.locality_match;
            decision.candidates.push_back(std::move(candidate));
        }

        if (decision.deferred_reason != FeasibilityReason::None) {
            decision.evidence = decision.park_reason;
            plan.decisions.push_back(std::move(decision));
            continue;
        }

        size_t selected = feasible.front();
        if (policy == "random") {
            const auto draw = random();
            selected = feasible[static_cast<size_t>(draw % feasible.size())];
            std::ostringstream evidence;
            evidence << "seed=" << seed << ";draw=" << draw
                     << ";candidate_count=" << feasible.size()
                     << ";selected_index=" << (draw % feasible.size());
            decision.evidence = evidence.str();
        } else if (policy == "constraint") {
            const auto job_profile = unit.members.empty() ? profile
                : profile_for_intent(profile, unit.members.front().intent);
            auto score = [&](size_t worker_index) {
                const auto& worker = prepared.workers[worker_index];
                const auto availability = 1.0;
                const auto capacity = remaining_ratio(worker, remaining[worker_index]);
                const auto inverse_load = 1.0 - std::clamp(worker.load, 0.0, 1.0);
                const auto speed = std::clamp(static_cast<double>(worker.speed) / 5.0, 0.0, 1.0);
                const auto energy = 1.0 - std::clamp((static_cast<double>(worker.energy) - 1.0) / 4.0, 0.0, 1.0);
                return job_profile.availability * availability + job_profile.capacity * capacity +
                       job_profile.load * inverse_load + job_profile.speed * speed +
                       job_profile.energy * energy + job_profile.tier *
                       (static_cast<double>(static_cast<uint8_t>(worker.tier)) / 2.0) +
                       job_profile.skills * worker.skills + job_profile.compute * worker.compute +
                       job_profile.reasoning * (static_cast<double>(worker.reasoning) / 5.0);
            };
            selected = *std::max_element(feasible.begin(), feasible.end(), [&](size_t left, size_t right) {
                const auto left_score = score(left);
                const auto right_score = score(right);
                if (left_score != right_score) return left_score < right_score;
                return prepared.workers[left].id > prepared.workers[right].id;
            });
            std::ostringstream evidence;
            evidence << "profile=" << (job_profile.name.empty() ? "default" : job_profile.name)
                     << ";version=1";
            decision.evidence = evidence.str();
            for (auto& candidate : decision.candidates) {
                const auto worker_it = std::find_if(prepared.workers.begin(), prepared.workers.end(),
                    [&](const WorkerInfo& worker) { return worker.id == candidate.worker_id; });
                if (worker_it == prepared.workers.end()) continue;
                const auto& worker = *worker_it;
                const auto cap = remaining_ratio(worker, candidate.available_capacity_before);
                const std::array<std::tuple<std::string, double, double>, 9> metrics{{
                    {"availability", 1.0, job_profile.availability},
                    {"capacity", cap, job_profile.capacity},
                    {"inverse_load", 1.0 - std::clamp(worker.load, 0.0, 1.0), job_profile.load},
                    {"speed", static_cast<double>(worker.speed) / 5.0, job_profile.speed},
                    {"inverse_energy", 1.0 - std::clamp((static_cast<double>(worker.energy) - 1.0) / 4.0, 0.0, 1.0), job_profile.energy},
                    {"tier", static_cast<double>(static_cast<uint8_t>(worker.tier)) / 2.0, job_profile.tier},
                    {"skills", worker.skills, job_profile.skills},
                    {"compute", worker.compute, job_profile.compute},
                    {"reasoning", static_cast<double>(worker.reasoning) / 5.0, job_profile.reasoning}
                }};
                double objective = 0.0;
                for (const auto& [name, normalized, weight] : metrics) {
                    const auto contribution = normalized * weight;
                    objective += contribution;
                    candidate.score_components.push_back({name, normalized, normalized, weight, contribution});
                }
                candidate.final_objective = objective;
            }
        } else if (policy == "minmax") {
            selected = *std::max_element(feasible.begin(), feasible.end(), [&](size_t left, size_t right) {
                const auto left_profit = profit(prepared.workers[left], remaining[left]);
                const auto right_profit = profit(prepared.workers[right], remaining[right]);
                if (left_profit != right_profit) return left_profit < right_profit;
                return prepared.workers[left].id > prepared.workers[right].id;
            });
            std::ostringstream evidence;
            evidence << "basis=" << (known ? "bytes" : "unit-count")
                     << ";spill_rank=0;objective=" << profit(prepared.workers[selected], remaining[selected]);
            decision.evidence = evidence.str();
            for (auto& candidate : decision.candidates) {
                const auto worker_it = std::find_if(prepared.workers.begin(), prepared.workers.end(),
                    [&](const WorkerInfo& worker) { return worker.id == candidate.worker_id; });
                if (worker_it != prepared.workers.end()) candidate.final_objective =
                    profit(*worker_it, candidate.available_capacity_before);
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
            rr_cursor.store(cursor);
            std::ostringstream evidence;
            evidence << "cursor_before=" << before << ";scan_rank=" << rank;
            decision.evidence = evidence.str();
        }

        decision.outcome = PlacementOutcome::Assigned;
        decision.worker_id = prepared.workers[selected].id;
        for (auto& candidate : decision.candidates) {
            if (candidate.worker_id == decision.worker_id) candidate.selected = true;
        }
        if (bytes != 0) remaining[selected] -= bytes;
        plan.decisions.push_back(std::move(decision));
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
        if (decision.outcome != PlacementOutcome::Assigned) continue;
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

} // namespace labios
