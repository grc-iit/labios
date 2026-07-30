#include <catch2/catch_test_macros.hpp>
#include <labios/scheduling/scheduling.h>

TEST_CASE("trace-guided Constraint uses completed-label service and throughput", "[scheduling][solver][trace]") {
    labios::SchedulingBatch batch;
    batch.batch_id = 42;
    labios::SchedulingUnitDescriptor unit;
    unit.unit_id = 7;
    unit.ordinal = 0;
    unit.members.push_back({
        .unit_id = 7,
        .label_id = 7,
        .ordinal = 0,
        .type = labios::LabelType::Write,
        .operation = "core.write",
        .operation_version = 1,
        .ir_version = labios::kCurrentIrVersion,
        .minimum_tier = labios::WorkerTier::Databot,
        .destinations = {{static_cast<uint8_t>(labios::ResourceFamily::FileRange),
                          "default", "file", "/trace/out", {}, false}},
        .demand = {1024, labios::DemandKind::Exact},
    });
    batch.units.push_back(unit);

    labios::WorkerInfo fast_static;
    fast_static.id = 1;
    fast_static.speed = 5;
    fast_static.total_capacity_bytes = 1 << 20;
    fast_static.available_capacity_bytes = 1 << 20;
    fast_static.operations = {"core.write"};
    fast_static.operation_versions = {1};
    fast_static.attachments = {{static_cast<uint8_t>(labios::ResourceFamily::FileRange),
                                "default", "file"}};
    fast_static.trace_samples = 8;
    fast_static.trace_service_us = 1000.0;
    fast_static.trace_queue_depth = 8.0;
    fast_static.trace_scheme_throughput["file"] = 10'000.0;

    labios::WorkerInfo trace_fast;
    trace_fast.id = 2;
    trace_fast.speed = 1;
    trace_fast.total_capacity_bytes = 1 << 20;
    trace_fast.available_capacity_bytes = 1 << 20;
    trace_fast.operations = {"core.write"};
    trace_fast.operation_versions = {1};
    trace_fast.attachments = {{static_cast<uint8_t>(labios::ResourceFamily::FileRange),
                               "default", "file"}};
    trace_fast.trace_samples = 8;
    trace_fast.trace_service_us = 100.0;
    trace_fast.trace_queue_depth = 1.0;
    trace_fast.trace_scheme_throughput["file"] = 100'000.0;

    auto prepared = labios::prepare_scheduling_batch(
        std::move(batch), {fast_static, trace_fast});
    auto profile = labios::WeightProfile{
        "trace_guided", 0.05, 0.15, 0.10, 0.15, 0.05, 0.0};
    profile.trace_service = 0.45;
    profile.trace_queue = 0.20;
    profile.trace_throughput = 0.35;
    const auto plan = labios::solve_prepared(prepared, "constraint", profile);

    REQUIRE(plan.decisions.size() == 1);
    CHECK(plan.decisions.front().worker_id == 2);
    REQUIRE(plan.decisions.front().candidates.size() == 2);
    const auto& selected = plan.decisions.front().candidates[1];
    CHECK(selected.selected);
    CHECK(selected.score_components.size() == 12);
    CHECK(selected.trace_sample_count == 8);
    CHECK(selected.trace_service_anchor == 100.0);
    CHECK(selected.trace_queue_anchor == profile.trace_queue_anchor);
    CHECK(selected.trace_throughput_anchor == 100'000.0);
    CHECK(plan.decisions.front().structured_policy_kind ==
          labios::StructuredPolicyKind::Constraint);
    CHECK(plan.decisions.front().constraint.profile_name == "trace_guided");
    CHECK(plan.decisions.front().evidence.find("trace=enabled") != std::string::npos);
}

TEST_CASE("trace-guided MinMax records trace objective evidence", "[scheduling][solver][trace]") {
    labios::SchedulingBatch batch;
    batch.batch_id = 43;
    labios::SchedulingUnitDescriptor unit;
    unit.unit_id = 8;
    unit.members.push_back({.unit_id = 8, .label_id = 8, .operation = "core.write",
                            .destinations = {{static_cast<uint8_t>(labios::ResourceFamily::FileRange),
                                              "default", "file", "/trace/out", {}, false}},
                            .demand = {1024, labios::DemandKind::Exact}});
    batch.units.push_back(unit);

    std::vector<labios::WorkerInfo> workers(2);
    for (int i = 0; i < 2; ++i) {
        workers[i].id = i + 1;
        workers[i].total_capacity_bytes = 1 << 20;
        workers[i].available_capacity_bytes = 1 << 20;
        workers[i].operations = {"core.write"};
        workers[i].operation_versions = {1};
        workers[i].attachments = {{static_cast<uint8_t>(labios::ResourceFamily::FileRange),
                                   "default", "file"}};
        workers[i].trace_samples = 4;
        workers[i].trace_service_us = i == 0 ? 1000.0 : 100.0;
        workers[i].trace_scheme_throughput["file"] = i == 0 ? 10'000.0 : 100'000.0;
    }
    const auto prepared = labios::prepare_scheduling_batch(std::move(batch), workers);
    auto profile = labios::WeightProfile{"trace_guided"};
    profile.trace_service = 0.3;
    profile.trace_queue = 0.1;
    profile.trace_throughput = 0.2;
    const auto plan = labios::solve_prepared(prepared, "minmax", profile);

    REQUIRE(plan.decisions.size() == 1);
    CHECK(plan.decisions.front().worker_id == 2);
    CHECK(plan.decisions.front().structured_policy_kind ==
          labios::StructuredPolicyKind::MinMax);
    CHECK(plan.decisions.front().minmax.profile_name == "trace_guided");
    CHECK(plan.decisions.front().candidates[1].trace_sample_count == 4);
    CHECK(plan.decisions.front().candidates[1].trace_service_anchor == 100.0);
    CHECK(plan.decisions.front().candidates[1].trace_throughput_anchor ==
          100'000.0);
    REQUIRE(plan.decisions.front().candidates[1].score_components.size() == 3);
    CHECK(plan.decisions.front().candidates[1].score_components[0].raw_value ==
          100.0);
    CHECK(plan.decisions.front().candidates[1].score_components[0]
              .normalized_value == 1.0);
    CHECK(plan.decisions.front().candidates[1].score_components[2].raw_value ==
          100'000.0);
    CHECK(plan.decisions.front().evidence.find("trace=enabled") != std::string::npos);
}

TEST_CASE("trace-guided MinMax explores workers below the sample threshold",
          "[scheduling][solver][trace]") {
    labios::SchedulingBatch batch;
    batch.batch_id = 44;
    labios::SchedulingUnitDescriptor unit;
    unit.unit_id = 9;
    unit.members.push_back({.unit_id = 9, .label_id = 9,
                            .operation = "core.write",
                            .destinations = {{static_cast<uint8_t>(
                                                  labios::ResourceFamily::FileRange),
                                              "default", "file", "/trace/out",
                                              {}, false}},
                            .demand = {1024, labios::DemandKind::Exact}});
    batch.units.push_back(unit);

    std::vector<labios::WorkerInfo> workers(2);
    for (int i = 0; i < 2; ++i) {
        workers[i].id = i + 1;
        workers[i].speed = 5;
        workers[i].total_capacity_bytes = 1 << 20;
        workers[i].available_capacity_bytes = 1 << 20;
        workers[i].operations = {"core.write"};
        workers[i].operation_versions = {1};
        workers[i].attachments = {{static_cast<uint8_t>(
                                        labios::ResourceFamily::FileRange),
                                    "default", "file"}};
    }
    workers[0].trace_samples = 3;
    workers[0].trace_service_us = 1.0;
    workers[0].trace_scheme_throughput["file"] = 1'000'000'000.0;

    const auto prepared =
        labios::prepare_scheduling_batch(std::move(batch), workers);
    auto profile = labios::WeightProfile{"trace_guided"};
    profile.trace_service = 0.45;
    profile.trace_queue = 0.20;
    profile.trace_throughput = 0.35;
    profile.trace_min_samples = 3;
    const auto plan = labios::solve_prepared(prepared, "minmax", profile);

    REQUIRE(plan.decisions.size() == 1);
    CHECK(plan.decisions.front().worker_id == 2);
    CHECK(plan.decisions.front().minmax.cold_exploration);
    CHECK(plan.decisions.front().tie_break.find(
              "bounded-cold-exploration") != std::string::npos);
}
