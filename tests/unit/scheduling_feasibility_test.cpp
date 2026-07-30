#include <catch2/catch_test_macros.hpp>
#include <labios/label.h>
#include <labios/scheduling/scheduling.h>
#include <labios/worker_registry_protocol.h>
#include <labios/worker_manager.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace {
labios::ResourceRequirement resource(uint8_t family, std::string backend,
                                     bool hard = false, std::string domain = {}) {
    labios::ResourceRequirement result;
    result.family = family;
    result.backend_id = std::move(backend);
    result.scheme = family == static_cast<uint8_t>(labios::ResourceFamily::FileRange)
        ? "file" : "sqlite";
    result.hard_locality = hard;
    result.locality_domain = std::move(domain);
    return result;
}

labios::WorkerInfo worker(int id, uint64_t available, labios::WorkerTier tier = labios::WorkerTier::Pipeline) {
    labios::WorkerInfo result;
    result.id = id;
    result.registration_epoch = 1;
    result.available = true;
    result.tier = tier;
    result.max_ir_version = 1;
    result.operations = {"core.write"};
    result.operation_versions = {1};
    result.pipeline_operations = {"builtin://identity"};
    result.pipeline_operation_versions = {1};
    result.total_capacity_bytes = available;
    result.available_capacity_bytes = available;
    result.capacity = 1.0;
    result.speed = 3;
    result.energy = 1;
    result.attachments = {
        {static_cast<uint8_t>(labios::ResourceFamily::FileRange), "source", "file", labios::LocalityKind::Shared, {}},
        {static_cast<uint8_t>(labios::ResourceFamily::Relational), "destination", "sqlite", labios::LocalityKind::Shared, {}}};
    return result;
}

labios::SchedulingUnitDescriptor unit(uint64_t id, uint64_t bytes, bool ready = true) {
    labios::JobDescriptor job;
    job.unit_id = id;
    job.label_id = id;
    job.ordinal = id;
    job.operation = "core.write";
    job.operation_version = 1;
    job.ir_version = 1;
    job.minimum_tier = labios::WorkerTier::Pipeline;
    job.pipeline_operations = {"builtin://identity"};
    job.pipeline_operation_versions = {1};
    job.sources = {resource(static_cast<uint8_t>(labios::ResourceFamily::FileRange), "source")};
    job.destinations = {resource(static_cast<uint8_t>(labios::ResourceFamily::Relational), "destination")};
    job.demand = {bytes, bytes == 0 ? labios::DemandKind::Unknown : labios::DemandKind::Exact};
    job.ready = ready;
    labios::SchedulingUnitDescriptor result;
    result.unit_id = id;
    result.ordinal = id;
    result.members = {std::move(job)};
    result.ready = ready;
    return result;
}

const labios::CandidateEvaluation& candidate(
    const labios::PlacementDecision& decision, int worker_id) {
    const auto it = std::find_if(
        decision.candidates.begin(), decision.candidates.end(),
        [&](const auto& value) { return value.worker_id == worker_id; });
    REQUIRE(it != decision.candidates.end());
    return *it;
}

const labios::ScoreComponent& component(
    const labios::CandidateEvaluation& value, std::string_view metric) {
    const auto it = std::find_if(
        value.score_components.begin(), value.score_components.end(),
        [&](const auto& item) { return item.metric == metric; });
    REQUIRE(it != value.score_components.end());
    return *it;
}
}

TEST_CASE("P09 mixed batch gets one ordered decision", "[scheduling][solver]") {
    labios::SchedulingBatch batch{42, 7, {unit(1, 4), unit(2, 4), unit(3, 4)}};
    auto prepared = labios::prepare_scheduling_batch(std::move(batch), {worker(10, 8), worker(20, 8)});
    auto plan = labios::solve_prepared(prepared, "round-robin");
    REQUIRE(plan.decisions.size() == 3);
    REQUIRE(labios::validate_plan(prepared, plan));
    CHECK(plan.decisions[0].unit_id == 1);
    CHECK(plan.decisions[1].unit_id == 2);
    CHECK(plan.decisions[2].unit_id == 3);
}

TEST_CASE("locality is not a feasibility bypass", "[scheduling]") {
    auto unavailable = worker(1, 64);
    unavailable.available = false;
    auto incompatible = worker(2, 64);
    incompatible.attachments.clear();
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{1, 1, {unit(1, 8)}}, {unavailable, incompatible});
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK_FALSE(prepared.matrix.values[0][1].feasible);
    CHECK(std::find(prepared.matrix.values[0][0].reasons.begin(), prepared.matrix.values[0][0].reasons.end(),
                    labios::FeasibilityReason::Unavailable) != prepared.matrix.values[0][0].reasons.end());
}

TEST_CASE("tier and pipeline capabilities are checked", "[scheduling]") {
    auto tier0 = worker(1, 64, labios::WorkerTier::Databot);
    auto tier1 = worker(2, 64, labios::WorkerTier::Pipeline);
    tier1.pipeline_operations.clear();
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{1, 1, {unit(1, 8)}}, {tier0, tier1});
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK_FALSE(prepared.matrix.values[0][1].feasible);
    CHECK(std::find(prepared.matrix.values[0][0].reasons.begin(), prepared.matrix.values[0][0].reasons.end(),
                    labios::FeasibilityReason::InsufficientTier) != prepared.matrix.values[0][0].reasons.end());
    CHECK(std::find(prepared.matrix.values[0][1].reasons.begin(), prepared.matrix.values[0][1].reasons.end(),
                    labios::FeasibilityReason::MissingPipelineOperation) != prepared.matrix.values[0][1].reasons.end());
}

TEST_CASE("empty or mismatched capability versions are infeasible", "[scheduling][worker-registry]") {
    auto missing_versions = worker(1, 64);
    missing_versions.operation_versions.clear();
    auto wrong_operation_version = worker(2, 64);
    wrong_operation_version.operation_versions = {2};
    auto wrong_pipeline_version = worker(3, 64);
    wrong_pipeline_version.pipeline_operation_versions = {2};
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{1, 1, {unit(1, 8)}},
        {missing_versions, wrong_operation_version, wrong_pipeline_version});
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK_FALSE(prepared.matrix.values[0][1].feasible);
    CHECK_FALSE(prepared.matrix.values[0][2].feasible);
    CHECK(std::find(prepared.matrix.values[0][0].reasons.begin(),
                    prepared.matrix.values[0][0].reasons.end(),
                    labios::FeasibilityReason::UnsupportedOperation) !=
          prepared.matrix.values[0][0].reasons.end());
    CHECK(std::find(prepared.matrix.values[0][2].reasons.begin(),
                    prepared.matrix.values[0][2].reasons.end(),
                    labios::FeasibilityReason::MissingPipelineOperation) !=
          prepared.matrix.values[0][2].reasons.end());
}

TEST_CASE("staged memory bindings do not require an external backend attachment",
          "[scheduling][staging]") {
    labios::LabelData label;
    label.id = 99;
    label.type = labios::LabelType::Write;
    label.operation = "core.write";
    label.operation_version = 1;
    label.ir_version = labios::kCurrentIrVersion;
    label.has_source_resource = true;
    label.source_resource.family = labios::ResourceFamily::Memory;
    label.has_destination_resource = true;
    label.destination_resource.family = labios::ResourceFamily::FileRange;
    label.destination_resource.backend_id = "default";
    label.destination_resource.path = "/staged/out";
    label.has_input_binding = true;
    label.input_binding.content_id = "99";
    label.input_binding.logical_length = 8;

    auto job = labios::describe_job(label, 0);
    REQUIRE(job.has_value());
    CHECK(job->sources.empty());
    REQUIRE(job->destinations.size() == 1);

    labios::SchedulingUnitDescriptor descriptor;
    descriptor.unit_id = label.id;
    descriptor.members.push_back(*job);
    auto candidate = worker(1, 64, labios::WorkerTier::Databot);
    candidate.operations = {"core.write"};
    candidate.operation_versions = {1};
    candidate.pipeline_operations.clear();
    candidate.pipeline_operation_versions.clear();
    candidate.attachments = {{
        static_cast<uint8_t>(labios::ResourceFamily::FileRange),
        "default", "file", labios::LocalityKind::Shared, {}}};

    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{1, 1, {descriptor}}, {candidate});
    CHECK(prepared.matrix.values[0][0].feasible);
}

TEST_CASE("both pipeline attachments are required", "[scheduling]") {
    auto source_only = worker(1, 64);
    source_only.attachments.pop_back();
    auto destination_only = worker(2, 64);
    destination_only.attachments.erase(destination_only.attachments.begin());
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{1, 1, {unit(1, 8)}}, {source_only, destination_only});
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK_FALSE(prepared.matrix.values[0][1].feasible);
    CHECK(std::count(prepared.matrix.values[0][0].reasons.begin(), prepared.matrix.values[0][0].reasons.end(),
                     labios::FeasibilityReason::MissingBackendAttachment) >= 1);
}

TEST_CASE("absolute and cumulative capacity reject placements", "[scheduling]") {
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{1, 1, {unit(1, 8)}}, {worker(1, 4)});
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK(std::find(prepared.matrix.values[0][0].reasons.begin(), prepared.matrix.values[0][0].reasons.end(),
                    labios::FeasibilityReason::InsufficientSingleJobCapacity) != prepared.matrix.values[0][0].reasons.end());

    auto cumulative = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{2, 1, {unit(1, 6), unit(2, 6)}}, {worker(1, 10)});
    auto plan = labios::solve_prepared(cumulative, "round-robin");
    CHECK(plan.decisions[0].outcome == labios::PlacementOutcome::Assigned);
    CHECK(plan.decisions[1].outcome == labios::PlacementOutcome::Deferred);
    CHECK(plan.decisions[1].candidates[0].reason_codes.back() == "EXHAUSTED_BATCH_CAPACITY");
}

TEST_CASE("known positive demand fails the zero-capacity common gate",
          "[scheduling][capacity]") {
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{11, 1, {unit(1, 1)}}, {worker(1, 0)});
    REQUIRE(prepared.matrix.values.size() == 1);
    REQUIRE(prepared.matrix.values[0].size() == 1);
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK(std::find(prepared.matrix.values[0][0].reasons.begin(),
                    prepared.matrix.values[0][0].reasons.end(),
                    labios::FeasibilityReason::InsufficientSingleJobCapacity) !=
          prepared.matrix.values[0][0].reasons.end());
    const auto plan = labios::solve_prepared(prepared, "minmax");
    CHECK(plan.decisions[0].outcome == labios::PlacementOutcome::Deferred);
    CHECK(candidate(plan.decisions[0], 1).reason_codes ==
          std::vector<std::string>{"INSUFFICIENT_SINGLE_JOB_CAPACITY"});
}

TEST_CASE("unknown demand records uncertainty without invented bytes", "[scheduling]") {
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{3, 1, {unit(1, 0)}}, {worker(1, 0)});
    auto plan = labios::solve_prepared(prepared, "constraint");
    REQUIRE(plan.decisions[0].outcome == labios::PlacementOutcome::Assigned);
    CHECK(plan.decisions[0].candidates[0].available_capacity_before == 0);
    CHECK(plan.decisions[0].candidates[0].available_capacity_after == 0);
}

TEST_CASE("blocked predecessor defers while unrelated work runs", "[scheduling]") {
    auto blocked = unit(1, 4, false);
    auto ready = unit(2, 4, true);
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{4, 1, {blocked, ready}}, {worker(1, 8)});
    auto plan = labios::solve_prepared(prepared, "round-robin");
    CHECK(plan.decisions[0].outcome == labios::PlacementOutcome::Deferred);
    CHECK(plan.decisions[0].park_reason == "BLOCKED_PREDECESSOR");
    CHECK(plan.decisions[1].outcome == labios::PlacementOutcome::Assigned);
}

TEST_CASE("constraint paper trace is deterministic and explainable", "[scheduling][solver]") {
    constexpr uint64_t MiB = 1024ULL * 1024ULL;
    auto paper_worker = [&](int id, uint64_t total, uint64_t available) {
        auto value = worker(id, total);
        value.total_capacity_bytes = total;
        value.available_capacity_bytes = available;
        value.attachments = {
            {static_cast<uint8_t>(labios::ResourceFamily::FileRange),
             "posix-main", "file", labios::LocalityKind::Shared, {}},
            {static_cast<uint8_t>(labios::ResourceFamily::Relational),
             "sqlite-main", "sqlite", labios::LocalityKind::Shared, {}}};
        return value;
    };
    auto paper_unit = unit(1, 8 * MiB);
    paper_unit.members[0].demand = {8 * MiB, labios::DemandKind::LowerBound};
    paper_unit.members[0].sources[0].backend_id = "posix-main";
    paper_unit.members[0].destinations[0].backend_id = "sqlite-main";
    auto w10 = paper_worker(10, 64 * MiB, 64 * MiB);
    w10.tier = labios::WorkerTier::Databot;
    auto w20 = paper_worker(20, 64 * MiB, 64 * MiB);
    w20.attachments.pop_back();
    auto w30 = paper_worker(30, 4 * MiB, 4 * MiB);
    auto w40 = paper_worker(40, 16 * MiB, 12 * MiB);
    w40.load = .25; w40.speed = 4; w40.energy = 2;
    auto w50 = paper_worker(50, 20 * MiB, 18 * MiB);
    w50.load = .10; w50.speed = 3; w50.energy = 1;
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{8, 1, {paper_unit}},
        {w10, w20, w30, w40, w50});
    labios::WeightProfile profile{"paper-trace/v1", .2, .2, .2, .2, .2, 0.0};
    auto plan = labios::solve_prepared(prepared, "constraint", profile);
    REQUIRE(plan.decisions[0].outcome == labios::PlacementOutcome::Assigned);
    CHECK(plan.decisions[0].worker_id == 50);
    REQUIRE(plan.decisions[0].candidates.size() == 5);
    CHECK(std::abs(candidate(plan.decisions[0], 40).final_objective - .81) < 1e-9);
    CHECK(std::abs(candidate(plan.decisions[0], 50).final_objective - .88) < 1e-9);
    const auto& speed = component(candidate(plan.decisions[0], 40), "speed");
    CHECK(speed.raw_value == 4.0);
    CHECK(speed.normalized_value == .8);
    CHECK(speed.weight == .2);
    CHECK(std::abs(speed.contribution - .16) < 1e-12);
    const auto& energy =
        component(candidate(plan.decisions[0], 40), "inverse_energy");
    CHECK(energy.raw_value == 2.0);
    CHECK(energy.normalized_value == .75);
    CHECK(std::abs(energy.contribution - .15) < 1e-12);
    CHECK(candidate(plan.decisions[0], 50).policy_rank == 1);
    CHECK(candidate(plan.decisions[0], 40).policy_rank == 2);
    CHECK(plan.decisions[0].structured_policy_kind ==
          labios::StructuredPolicyKind::Constraint);
    CHECK(plan.decisions[0].constraint.profile_name == "paper-trace/v1");
    CHECK(plan.decisions[0].tie_break ==
          "soft-locality,ascending-worker-id");
    CHECK(std::find(plan.decisions[0].candidates[0].reason_codes.begin(), plan.decisions[0].candidates[0].reason_codes.end(),
                    "INSUFFICIENT_TIER") != plan.decisions[0].candidates[0].reason_codes.end());
    CHECK(std::find(plan.decisions[0].candidates[1].reason_codes.begin(), plan.decisions[0].candidates[1].reason_codes.end(),
                    "MISSING_BACKEND_ATTACHMENT") != plan.decisions[0].candidates[1].reason_codes.end());

    const auto history = labios::make_decision_snapshot(
        prepared, 0, plan.decisions[0], 1, "constraint");
    CHECK(history.reservation_bytes == 8 * MiB);
    CHECK_FALSE(history.complete_size_known);
    labios::LabelData label;
    label.id = 1;
    labios::apply_scheduling_decision(
        label, history, plan.decisions[0], &prepared.workers[4],
        "constraint", 100);
    CHECK(label.routing.worker_id == 50);
    CHECK(label.score_snapshot.speed == 3.0);
    REQUIRE(label.score_snapshot.decisions.size() == 1);
}

TEST_CASE("priority stays within the legal ready frontier", "[scheduling]") {
    auto blocked = unit(1, 1, false);
    auto high = unit(2, 1, true); high.members[0].priority = 255;
    auto low = unit(3, 1, true); low.members[0].priority = 1;
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{6, 1, {blocked, high, low}}, {worker(1, 8)});
    auto plan = labios::solve_prepared(prepared, "round-robin");
    CHECK(plan.decisions[0].outcome == labios::PlacementOutcome::Deferred);
    CHECK(plan.decisions[1].outcome == labios::PlacementOutcome::Assigned);
    CHECK(plan.decisions[2].outcome == labios::PlacementOutcome::Assigned);
}

TEST_CASE("hard locality gates feasibility and soft locality breaks policy ties",
          "[scheduling][locality]") {
    auto preferred = unit(1, 1);
    preferred.members[0].sources[0].locality_domain = "rack:b";
    auto w1 = worker(1, 16);
    auto w2 = worker(2, 16);
    w2.locality_domains = {"rack:b"};

    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{12, 1, {preferred}}, {w1, w2});
    CHECK_FALSE(prepared.matrix.values[0][0].locality_match);
    CHECK(prepared.matrix.values[0][1].locality_match);
    auto constraint = labios::solve_prepared(prepared, "constraint");
    CHECK(constraint.decisions[0].worker_id == 2);
    auto minmax = labios::solve_prepared(prepared, "minmax");
    CHECK(minmax.decisions[0].worker_id == 2);

    auto hard = preferred;
    hard.members[0].sources[0].hard_locality = true;
    auto hard_prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{13, 1, {hard}}, {w1, w2});
    CHECK_FALSE(hard_prepared.matrix.values[0][0].feasible);
    CHECK(std::find(
              hard_prepared.matrix.values[0][0].reasons.begin(),
              hard_prepared.matrix.values[0][0].reasons.end(),
              labios::FeasibilityReason::HardLocalityMismatch) !=
          hard_prepared.matrix.values[0][0].reasons.end());
    CHECK(hard_prepared.matrix.values[0][1].feasible);
}

TEST_CASE("Composite demand and feasibility are all-or-nothing",
          "[scheduling][composite]") {
    auto first = unit(100, 4);
    auto second = unit(101, 5);
    labios::SchedulingUnitDescriptor composite;
    composite.unit_id = 900;
    composite.ordinal = 1;
    composite.members = {first.members[0], second.members[0]};
    auto small = worker(1, 8);
    auto sufficient = worker(2, 10);
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{14, 1, {composite}}, {small, sufficient});
    CHECK_FALSE(prepared.matrix.values[0][0].feasible);
    CHECK(prepared.matrix.values[0][1].feasible);
    auto plan = labios::solve_prepared(prepared, "round-robin");
    REQUIRE(plan.decisions.size() == 1);
    CHECK(plan.decisions[0].outcome == labios::PlacementOutcome::Assigned);
    CHECK(plan.decisions[0].worker_id == 2);
    const auto history = labios::make_decision_snapshot(
        prepared, 0, plan.decisions[0], 1, "round-robin");
    CHECK(history.reservation_bytes == 9);

    composite.members[0].demand = {
        std::numeric_limits<uint64_t>::max(), labios::DemandKind::Exact};
    composite.members[1].demand = {1, labios::DemandKind::Exact};
    auto overflow = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{15, 1, {composite}}, {sufficient});
    CHECK_FALSE(overflow.matrix.values[0][0].feasible);
    CHECK(std::find(overflow.matrix.values[0][0].reasons.begin(),
                    overflow.matrix.values[0][0].reasons.end(),
                    labios::FeasibilityReason::Invalid) !=
          overflow.matrix.values[0][0].reasons.end());
}

TEST_CASE("MinMax distributes known byte demand by target share and spills",
          "[scheduling][solver][minmax]") {
    auto w1 = worker(1, 64);
    auto w2 = worker(2, 64);
    w1.speed = 5;
    w2.speed = 1;
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{
            16, 1, {unit(1, 6), unit(2, 6), unit(3, 6), unit(4, 6)}},
        {w1, w2});
    const auto plan = labios::solve_prepared(prepared, "minmax");
    REQUIRE(plan.decisions.size() == 4);
    CHECK(plan.decisions[0].worker_id == 1);
    CHECK(plan.decisions[1].worker_id == 1);
    CHECK(plan.decisions[2].worker_id == 1);
    CHECK(plan.decisions[3].worker_id == 2);
    for (const auto& decision : plan.decisions) {
        CHECK(decision.minmax.demand_basis ==
              labios::SchedulingDemandBasis::ReservationBytes);
        CHECK(decision.minmax.total_demand == 24);
        REQUIRE(decision.minmax.workers.size() == 2);
        CHECK(std::abs(decision.minmax.workers[0].target_share -
                       (5.0 / 6.0)) < 1e-12);
        CHECK(std::abs(decision.minmax.workers[0].target_amount - 20.0) <
              1e-12);
    }
    CHECK(plan.decisions[3].minmax.workers[1].spill_rank == 1);
    CHECK(candidate(plan.decisions[0], 1).available_capacity_after == 58);
    CHECK(candidate(plan.decisions[0], 2).available_capacity_after == 64);
    CHECK(labios::validate_plan(prepared, plan));
}

TEST_CASE("MinMax uses unit-count shares for unknown demand without bytes",
          "[scheduling][solver][minmax]") {
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{
            17, 1, {unit(1, 0), unit(2, 0), unit(3, 0), unit(4, 0)}},
        {worker(1, 0), worker(2, 0)});
    const auto plan = labios::solve_prepared(prepared, "minmax");
    REQUIRE(plan.decisions.size() == 4);
    CHECK(plan.decisions[0].worker_id == 1);
    CHECK(plan.decisions[1].worker_id == 1);
    CHECK(plan.decisions[2].worker_id == 2);
    CHECK(plan.decisions[3].worker_id == 2);
    for (const auto& decision : plan.decisions) {
        CHECK(decision.minmax.demand_basis ==
              labios::SchedulingDemandBasis::UnitCount);
        CHECK(decision.minmax.total_demand == 4);
        CHECK(candidate(decision, 1).available_capacity_before == 0);
        CHECK(candidate(decision, 1).available_capacity_after == 0);
    }
}

TEST_CASE("MinMax accounts for unequal unit size before crossing target shares",
          "[scheduling][solver][minmax]") {
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{
            18, 1, {unit(1, 49), unit(2, 49), unit(3, 2)}},
        {worker(1, 100), worker(2, 100)});
    const auto plan = labios::solve_prepared(prepared, "minmax");
    REQUIRE(plan.decisions.size() == 3);
    CHECK(plan.decisions[0].worker_id == 1);
    CHECK(plan.decisions[1].worker_id == 2);
    CHECK(plan.decisions[2].worker_id == 1);
    CHECK(plan.decisions[2].minmax.workers[0].consumption_after == 51.0);
    CHECK(plan.decisions[2].minmax.workers[1].consumption_after == 49.0);
}

TEST_CASE("policy identities retain deterministic replay evidence", "[scheduling][solver]") {
    auto rejected = worker(1, 64); rejected.available = false;
    auto feasible = worker(2, 64);
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{7, 1, {unit(1, 8)}}, {rejected, feasible});
    auto random_plan = labios::solve_prepared(prepared, "random");
    REQUIRE(random_plan.decisions[0].worker_id == 2);
    CHECK(random_plan.decisions[0].structured_policy_kind ==
          labios::StructuredPolicyKind::Random);
    CHECK(random_plan.decisions[0].random.batch_seed == 7);
    CHECK(random_plan.decisions[0].random.candidate_count == 1);
    CHECK(random_plan.decisions[0].random.selected_index == 0);
    CHECK(random_plan.decisions[0].evidence.find("candidate_count=1") != std::string::npos);
    auto minmax_plan = labios::solve_prepared(prepared, "minmax");
    REQUIRE(minmax_plan.decisions[0].worker_id == 2);
    CHECK(minmax_plan.decisions[0].structured_policy_kind ==
          labios::StructuredPolicyKind::MinMax);
    CHECK(minmax_plan.decisions[0].evidence.find("basis=bytes") != std::string::npos);
    auto rr_plan = labios::solve_prepared(prepared, "round-robin");
    CHECK(rr_plan.decisions[0].structured_policy_kind ==
          labios::StructuredPolicyKind::RoundRobin);
    CHECK(rr_plan.decisions[0].round_robin.cursor_worker_id_before > 0);
    CHECK(rr_plan.decisions[0].round_robin.selected_scan_rank <= 1);
}

TEST_CASE("all policies defer explicitly when no feasible worker exists", "[scheduling][solver]") {
    auto unavailable = worker(1, 64); unavailable.available = false;
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{5, 1, {unit(1, 8)}}, {unavailable});
    for (const auto policy : {"round-robin", "random", "constraint", "minmax"}) {
        auto plan = labios::solve_prepared(prepared, policy);
        REQUIRE(plan.decisions.size() == 1);
        CHECK(plan.decisions[0].outcome == labios::PlacementOutcome::Deferred);
        CHECK(plan.decisions[0].deferred_reason == labios::FeasibilityReason::NoAvailableWorker);
    }
}

TEST_CASE("registry v2 verifies typed messages and rejects malformed data", "[worker-registry]") {
    auto item = worker(7, 64);
    item.registration_epoch = 9;
    auto registration = labios::encode_worker_registration(item);
    auto decoded = labios::decode_worker_message(registration);
    REQUIRE(decoded.kind == labios::WorkerRegistryMessage::Kind::Registration);
    CHECK(decoded.worker.id == 7);
    CHECK(decoded.worker.registration_epoch == 9);
    const std::vector<labios::WorkerInfo> items{item};
    auto snapshot = labios::encode_worker_snapshot(items, 3, 100);
    auto snapshot_decoded = labios::decode_worker_message(snapshot);
    CHECK(snapshot_decoded.registry_generation == 3);
    CHECK_THROWS(labios::decode_worker_message(std::span<const std::byte>(registration.data(), registration.size() - 8)));
    registration[4] = std::byte{'X'};
    CHECK_THROWS(labios::decode_worker_message(registration));

    labios::InMemoryWorkerManager manager;
    CHECK(manager.register_worker_v2(item));
    const auto generation = manager.registry_generation();
    CHECK_FALSE(manager.deregister_worker_v2(item.id, item.registration_epoch - 1));
    CHECK(manager.registry_generation() == generation);
    CHECK(manager.deregister_worker_v2(item.id, item.registration_epoch));
    CHECK(manager.registry_generation() == generation + 1);
}

TEST_CASE("structured policy evidence round-trips every policy variant",
          "[scheduling][label]") {
    labios::LabelData label;
    label.id = 900;
    labios::SchedulingDecisionSnapshot rr;
    rr.decision_id = 1;
    rr.outcome = "Assigned";
    rr.policy_name = "round-robin";
    rr.policy_version = 1;
    rr.tie_break = "circular-ascending-worker-id";
    rr.structured_policy_kind = labios::StructuredPolicyKind::RoundRobin;
    rr.round_robin = {20, 2};
    label.score_snapshot.decisions.push_back(rr);

    labios::SchedulingDecisionSnapshot random;
    random.decision_id = 2;
    random.outcome = "Assigned";
    random.policy_name = "random";
    random.structured_policy_kind = labios::StructuredPolicyKind::Random;
    random.random = {0x1'0000'0001ULL, 0x2'0000'0002ULL, 3, 2};
    label.score_snapshot.decisions.push_back(random);

    labios::SchedulingDecisionSnapshot constraint;
    constraint.decision_id = 3;
    constraint.outcome = "Assigned";
    constraint.policy_name = "constraint";
    constraint.structured_policy_kind =
        labios::StructuredPolicyKind::Constraint;
    constraint.constraint = {"paper-trace/v1", 1};
    labios::CandidateEvaluation scored;
    scored.worker_id = 50;
    scored.feasible = true;
    scored.available_capacity_before = 18;
    scored.available_capacity_after = 10;
    scored.locality_match = true;
    scored.policy_rank = 1;
    scored.selected = true;
    scored.trace_sample_count = 7;
    scored.trace_service_anchor = 100.0;
    scored.trace_queue_anchor = 8.0;
    scored.trace_throughput_anchor = 1024.0;
    scored.score_components.push_back(
        {"speed", 3.0, .6, .2, .12});
    constraint.candidates.push_back(scored);
    label.score_snapshot.decisions.push_back(constraint);

    labios::SchedulingDecisionSnapshot minmax;
    minmax.decision_id = 4;
    minmax.outcome = "Parked";
    minmax.park_reason = "NO_FEASIBLE_CURRENT_PLACEMENT";
    minmax.policy_name = "minmax";
    minmax.structured_policy_kind = labios::StructuredPolicyKind::MinMax;
    minmax.minmax.demand_basis =
        labios::SchedulingDemandBasis::ReservationBytes;
    minmax.minmax.total_demand = 24;
    minmax.minmax.final_batch_objective = 19.5;
    minmax.minmax.profile_name = "trace-guided";
    minmax.minmax.cold_exploration = true;
    minmax.minmax.workers.push_back(
        {50, 2.0, .4, 9.6, 6.0, 6.0, 1});
    label.score_snapshot.decisions.push_back(minmax);
    label.score_snapshot.decision_version = 1;

    auto round_trip = labios::deserialize_label(labios::serialize_label(label));
    CHECK(round_trip.score_snapshot.availability == 0.0);
    REQUIRE(round_trip.score_snapshot.decisions.size() == 4);
    CHECK(round_trip.score_snapshot.decisions[0].round_robin
              .cursor_worker_id_before == 20);
    CHECK(round_trip.score_snapshot.decisions[0].round_robin
              .selected_scan_rank == 2);
    CHECK(round_trip.score_snapshot.decisions[1].random.batch_seed ==
          0x1'0000'0001ULL);
    CHECK(round_trip.score_snapshot.decisions[1].random.raw_draw ==
          0x2'0000'0002ULL);
    CHECK(round_trip.score_snapshot.decisions[2].constraint.profile_name ==
          "paper-trace/v1");
    REQUIRE(round_trip.score_snapshot.decisions[2].candidates.size() == 1);
    CHECK(round_trip.score_snapshot.decisions[2].candidates[0]
              .score_components[0].raw_value == 3.0);
    CHECK(round_trip.score_snapshot.decisions[2].candidates[0]
              .score_components[0].normalized_value == .6);
    CHECK(round_trip.score_snapshot.decisions[2].candidates[0]
              .trace_sample_count == 7);
    CHECK(round_trip.score_snapshot.decisions[3].outcome == "Parked");
    CHECK(round_trip.score_snapshot.decisions[3].minmax.demand_basis ==
          labios::SchedulingDemandBasis::ReservationBytes);
    REQUIRE(round_trip.score_snapshot.decisions[3].minmax.workers.size() == 1);
    CHECK(round_trip.score_snapshot.decisions[3].minmax.workers[0]
              .target_share == .4);
}

TEST_CASE("serialized production snapshots deterministically replay assignment and parking",
          "[scheduling][label][replay]") {
    auto excluded = worker(30, 12);
    excluded.attachments.clear();
    auto original = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{
            0xABC, 77, {unit(101, 6), unit(102, 6), unit(103, 6, false)}},
        {worker(10, 12), worker(20, 12), excluded});
    const auto original_plan = labios::solve_prepared(original, "minmax");
    REQUIRE(original_plan.decisions.size() == 3);
    CHECK(original_plan.decisions[0].worker_id == 10);
    CHECK(original_plan.decisions[1].worker_id == 20);
    CHECK(original_plan.decisions[2].outcome ==
          labios::PlacementOutcome::Deferred);

    std::vector<labios::SchedulingDecisionSnapshot> decoded;
    for (size_t index = 0; index < original_plan.decisions.size(); ++index) {
        const auto history = labios::make_decision_snapshot(
            original, index, original_plan.decisions[index], 1, "minmax");
        labios::LabelData label;
        label.id = original.batch.units[index].unit_id;
        const auto worker_it = std::find_if(
            original.workers.begin(), original.workers.end(),
            [&](const auto& value) {
                return value.id == original_plan.decisions[index].worker_id;
            });
        labios::apply_scheduling_decision(
            label, history, original_plan.decisions[index],
            worker_it == original.workers.end() ? nullptr : &*worker_it,
            "minmax", 100 + index);
        const auto restored =
            labios::deserialize_label(labios::serialize_label(label));
        REQUIRE(restored.score_snapshot.decisions.size() == 1);
        decoded.push_back(restored.score_snapshot.decisions[0]);
    }

    auto replay_prepared =
        labios::reconstruct_prepared_scheduling_batch(decoded);
    const auto replay_profile =
        labios::reconstruct_weight_profile(decoded.front());
    const auto replay_plan = labios::solve_prepared(
        replay_prepared, decoded.front().policy_name, replay_profile);
    REQUIRE(replay_plan.decisions.size() == decoded.size());

    for (size_t index = 0; index < decoded.size(); ++index) {
        const auto replayed = labios::make_decision_snapshot(
            replay_prepared, index, replay_plan.decisions[index], 1,
            "minmax");
        const auto& recorded = decoded[index];
        CHECK(replayed.outcome == recorded.outcome);
        CHECK(replayed.chosen_worker_id == recorded.chosen_worker_id);
        CHECK(replayed.park_reason == recorded.park_reason);
        CHECK(replayed.tie_break == recorded.tie_break);
        REQUIRE(replayed.candidates.size() == recorded.candidates.size());
        for (size_t candidate_index = 0;
             candidate_index < recorded.candidates.size();
             ++candidate_index) {
            const auto& actual = replayed.candidates[candidate_index];
            const auto& expected = recorded.candidates[candidate_index];
            CHECK(actual.worker_id == expected.worker_id);
            CHECK(actual.feasible == expected.feasible);
            CHECK(actual.reason_codes == expected.reason_codes);
            CHECK(actual.policy_rank == expected.policy_rank);
            CHECK(actual.locality_match == expected.locality_match);
            CHECK(actual.final_objective == expected.final_objective);
            CHECK(actual.available_capacity_before ==
                  expected.available_capacity_before);
            CHECK(actual.available_capacity_after ==
                  expected.available_capacity_after);
            CHECK(actual.selected == expected.selected);
        }
        REQUIRE(replayed.minmax.workers.size() ==
                recorded.minmax.workers.size());
        CHECK(replayed.minmax.final_batch_objective ==
              recorded.minmax.final_batch_objective);
        for (size_t worker_index = 0;
             worker_index < recorded.minmax.workers.size(); ++worker_index) {
            CHECK(replayed.minmax.workers[worker_index].profit ==
                  recorded.minmax.workers[worker_index].profit);
            CHECK(replayed.minmax.workers[worker_index].target_share ==
                  recorded.minmax.workers[worker_index].target_share);
            CHECK(replayed.minmax.workers[worker_index].consumption_before ==
                  recorded.minmax.workers[worker_index].consumption_before);
            CHECK(replayed.minmax.workers[worker_index].consumption_after ==
                  recorded.minmax.workers[worker_index].consumption_after);
            CHECK(replayed.minmax.workers[worker_index].spill_rank ==
                  recorded.minmax.workers[worker_index].spill_rank);
        }
    }
}
