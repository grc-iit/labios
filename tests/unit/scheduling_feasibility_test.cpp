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
    auto w10 = worker(10, 64); w10.tier = labios::WorkerTier::Databot;
    auto w20 = worker(20, 64); w20.attachments.pop_back();
    auto w30 = worker(30, 4);
    auto w40 = worker(40, 16); w40.available_capacity_bytes = 12; w40.load = .25; w40.speed = 4; w40.energy = 2;
    auto w50 = worker(50, 20); w50.available_capacity_bytes = 18; w50.load = .10; w50.speed = 3; w50.energy = 1;
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{8, 1, {unit(1, 8)}}, {w10, w20, w30, w40, w50});
    labios::WeightProfile profile{"paper-trace/v1", .2, .2, .2, .2, .2, 0.0};
    auto plan = labios::solve_prepared(prepared, "constraint", profile);
    REQUIRE(plan.decisions[0].outcome == labios::PlacementOutcome::Assigned);
    CHECK(plan.decisions[0].worker_id == 50);
    REQUIRE(plan.decisions[0].candidates.size() == 5);
    CHECK(std::abs(plan.decisions[0].candidates[3].final_objective - .81) < 1e-9);
    CHECK(std::abs(plan.decisions[0].candidates[4].final_objective - .88) < 1e-9);
    CHECK(std::find(plan.decisions[0].candidates[0].reason_codes.begin(), plan.decisions[0].candidates[0].reason_codes.end(),
                    "INSUFFICIENT_TIER") != plan.decisions[0].candidates[0].reason_codes.end());
    CHECK(std::find(plan.decisions[0].candidates[1].reason_codes.begin(), plan.decisions[0].candidates[1].reason_codes.end(),
                    "MISSING_BACKEND_ATTACHMENT") != plan.decisions[0].candidates[1].reason_codes.end());
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

TEST_CASE("policy identities retain deterministic replay evidence", "[scheduling][solver]") {
    auto rejected = worker(1, 64); rejected.available = false;
    auto feasible = worker(2, 64);
    auto prepared = labios::prepare_scheduling_batch(
        labios::SchedulingBatch{7, 1, {unit(1, 8)}}, {rejected, feasible});
    auto random_plan = labios::solve_prepared(prepared, "random");
    REQUIRE(random_plan.decisions[0].worker_id == 2);
    CHECK(random_plan.decisions[0].evidence.find("candidate_count=1") != std::string::npos);
    auto minmax_plan = labios::solve_prepared(prepared, "minmax");
    REQUIRE(minmax_plan.decisions[0].worker_id == 2);
    CHECK(minmax_plan.decisions[0].evidence.find("basis=bytes") != std::string::npos);
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

TEST_CASE("decision history replays assigned and parked attempts", "[scheduling][label]") {
    labios::LabelData label;
    label.id = 900;
    label.score_snapshot.decisions.push_back({1, 2, 900, 1, 3, 0, "Assigned", 50, {}, 8, false, {}, "constraint", "score=0.88"});
    label.score_snapshot.decisions.push_back({2, 3, 900, 2, 4, 0, "Parked", -1, "NO_FEASIBLE_CURRENT_PLACEMENT", 8, false, {}, "constraint", "park"});
    label.score_snapshot.decision_version = 1;
    auto round_trip = labios::deserialize_label(labios::serialize_label(label));
    CHECK(round_trip.score_snapshot.availability == 0.0);
    REQUIRE(round_trip.score_snapshot.decisions.size() == 2);
    CHECK(round_trip.score_snapshot.decisions[0].chosen_worker_id == 50);
    CHECK(round_trip.score_snapshot.decisions[1].outcome == "Parked");
    CHECK(round_trip.score_snapshot.decisions[1].park_reason == "NO_FEASIBLE_CURRENT_PLACEMENT");
}
