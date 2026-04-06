#include <labios/elastic/decision_engine.h>
#include <labios/elastic/orchestrator.h>
#include <labios/elastic/docker_client.h>
#include <catch2/catch_test_macros.hpp>

using namespace labios::elastic;
using namespace labios;
using clk = std::chrono::steady_clock;

// --- QueueBreakdown parsing ---

TEST_CASE("Parse queue breakdown with all three fields", "[elastic][tiered]") {
    auto qb = parse_queue_breakdown("100,15,3");
    CHECK(qb.total == 100);
    CHECK(qb.with_pipeline == 15);
    CHECK(qb.observe_count == 3);
}

TEST_CASE("Parse queue breakdown with only total (backwards compat)", "[elastic][tiered]") {
    auto qb = parse_queue_breakdown("42");
    CHECK(qb.total == 42);
    CHECK(qb.with_pipeline == 0);
    CHECK(qb.observe_count == 0);
}

TEST_CASE("Parse queue breakdown with empty string", "[elastic][tiered]") {
    auto qb = parse_queue_breakdown("");
    CHECK(qb.total == 0);
    CHECK(qb.with_pipeline == 0);
    CHECK(qb.observe_count == 0);
}

TEST_CASE("Parse queue breakdown with two fields", "[elastic][tiered]") {
    auto qb = parse_queue_breakdown("50,8");
    CHECK(qb.total == 50);
    CHECK(qb.with_pipeline == 8);
    CHECK(qb.observe_count == 0);
}

// --- Tiered evaluation helpers ---

static TieredSnapshot base_tiered() {
    return {
        .queue = {100, 0, 0},
        .pressure_count = 0,
        .pressure_threshold = 5,
        .databot = {2, 1, 10, {}, {}},
        .pipeline = {0, 0, 5, {}, {}},
        .agentic = {0, 0, 2, {}, {}},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::seconds(5),
    };
}

// --- Commission decisions ---

TEST_CASE("High pressure, no SDS labels -> commission Databot", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.queue.with_pipeline = 0;

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Commission);
    CHECK(d.target_tier == WorkerTier::Databot);
}

TEST_CASE("High pressure, SDS labels, no pipeline workers -> commission Pipeline",
          "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.queue.with_pipeline = 10;
    snap.pipeline.active = 0;
    snap.agentic.active = 0;

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Commission);
    CHECK(d.target_tier == WorkerTier::Pipeline);
}

TEST_CASE("High pressure, SDS labels, pipeline worker exists -> commission Databot",
          "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.queue.with_pipeline = 10;
    snap.pipeline.active = 1;

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Commission);
    CHECK(d.target_tier == WorkerTier::Databot);
}

TEST_CASE("High pressure, SDS labels, agentic worker handles pipelines",
          "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.queue.with_pipeline = 10;
    snap.pipeline.active = 0;
    snap.agentic.active = 1;  // Agentic can handle pipeline work

    auto d = evaluate_tiered(snap);
    // Should commission Databot since pipeline+agentic > 0
    CHECK(d.action == Action::Commission);
    CHECK(d.target_tier == WorkerTier::Databot);
}

// --- Per-tier max limits ---

TEST_CASE("Databot max respected, falls through to pipeline on SDS demand",
          "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.queue.with_pipeline = 5;
    snap.databot.active = 10;
    snap.databot.max = 10;  // At max
    snap.pipeline.active = 0;
    snap.agentic.active = 0;

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Commission);
    CHECK(d.target_tier == WorkerTier::Pipeline);
}

// --- Resume preferred over commission ---

TEST_CASE("Resume suspended Databot instead of commissioning new", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.databot.suspended_ids = {101};

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Resume);
    CHECK(d.target_tier == WorkerTier::Databot);
    CHECK(d.target_worker_id == 101);
}

TEST_CASE("Resume suspended Pipeline when SDS demand present", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 5;
    snap.queue.with_pipeline = 10;
    snap.pipeline.active = 0;
    snap.agentic.active = 0;
    snap.pipeline.suspended_ids = {201};

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Resume);
    CHECK(d.target_tier == WorkerTier::Pipeline);
    CHECK(d.target_worker_id == 201);
}

// --- Decommission: lowest tier first ---

TEST_CASE("Idle Databot decommissioned before idle Pipeline", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 0;
    snap.databot.active = 3;
    snap.databot.min = 1;
    snap.databot.idle_ids = {100};
    snap.pipeline.active = 2;
    snap.pipeline.min = 0;
    snap.pipeline.idle_ids = {200};

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Decommission);
    CHECK(d.target_tier == WorkerTier::Databot);
    CHECK(d.target_worker_id == 100);
}

TEST_CASE("Pipeline decommissioned when no idle Databots", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 0;
    snap.databot.active = 1;
    snap.databot.min = 1;
    snap.databot.idle_ids = {};
    snap.pipeline.active = 2;
    snap.pipeline.min = 0;
    snap.pipeline.idle_ids = {200};

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::Decommission);
    CHECK(d.target_tier == WorkerTier::Pipeline);
    CHECK(d.target_worker_id == 200);
}

TEST_CASE("No decommission when at per-tier minimum", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 0;
    snap.databot.active = 1;
    snap.databot.min = 1;
    snap.databot.idle_ids = {100};  // Would decommission but at min

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::None);
}

TEST_CASE("No decommission while queue has pressure", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 3;
    snap.databot.idle_ids = {100};
    snap.databot.active = 3;
    snap.databot.min = 1;

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::None);
}

// --- Cooldown ---

TEST_CASE("Cooldown prevents tiered commission", "[elastic][tiered]") {
    auto snap = base_tiered();
    snap.pressure_count = 10;
    snap.last_commission = clk::now() - std::chrono::seconds(1);
    snap.cooldown = std::chrono::seconds(5);

    auto d = evaluate_tiered(snap);
    CHECK(d.action == Action::None);
}

// --- Worker manager tier queries ---

TEST_CASE("workers_by_tier returns correct subset", "[elastic][tiered]") {
    InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1, WorkerTier::Databot});
    mgr.register_worker({2, true, 0.8, 0.2, 4, 2, WorkerTier::Pipeline});
    mgr.register_worker({3, true, 0.7, 0.3, 3, 3, WorkerTier::Databot});
    mgr.register_worker({4, true, 0.6, 0.4, 2, 4, WorkerTier::Agentic});

    auto databots = mgr.workers_by_tier(WorkerTier::Databot);
    CHECK(databots.size() == 2);

    auto pipelines = mgr.workers_by_tier(WorkerTier::Pipeline);
    CHECK(pipelines.size() == 1);
    CHECK(pipelines[0].id == 2);

    auto agentics = mgr.workers_by_tier(WorkerTier::Agentic);
    CHECK(agentics.size() == 1);
    CHECK(agentics[0].id == 4);
}

TEST_CASE("count_by_tier returns correct counts", "[elastic][tiered]") {
    InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1, WorkerTier::Databot});
    mgr.register_worker({2, true, 0.8, 0.2, 4, 2, WorkerTier::Pipeline});
    mgr.register_worker({3, true, 0.7, 0.3, 3, 3, WorkerTier::Databot});

    CHECK(mgr.count_by_tier(WorkerTier::Databot) == 2);
    CHECK(mgr.count_by_tier(WorkerTier::Pipeline) == 1);
    CHECK(mgr.count_by_tier(WorkerTier::Agentic) == 0);
}

TEST_CASE("suspended_workers_by_tier filters correctly", "[elastic][tiered]") {
    InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1, WorkerTier::Databot});
    mgr.register_worker({2, true, 0.8, 0.2, 4, 2, WorkerTier::Pipeline});
    mgr.register_worker({3, true, 0.7, 0.3, 3, 3, WorkerTier::Databot});

    // Suspend one databot and the pipeline worker.
    mgr.update_score(1, {1, false, 0.9, 0.1, 5, 1, WorkerTier::Databot});
    mgr.update_score(2, {2, false, 0.8, 0.2, 4, 2, WorkerTier::Pipeline});

    auto susp_databot = mgr.suspended_workers_by_tier(WorkerTier::Databot);
    CHECK(susp_databot.size() == 1);
    CHECK(susp_databot[0] == 1);

    auto susp_pipeline = mgr.suspended_workers_by_tier(WorkerTier::Pipeline);
    CHECK(susp_pipeline.size() == 1);
    CHECK(susp_pipeline[0] == 2);

    auto susp_agentic = mgr.suspended_workers_by_tier(WorkerTier::Agentic);
    CHECK(susp_agentic.empty());
}

TEST_CASE("decommissionable_workers_by_tier respects tier and threshold",
          "[elastic][tiered]") {
    InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1, WorkerTier::Databot});
    mgr.register_worker({2, true, 0.8, 0.2, 4, 2, WorkerTier::Pipeline});

    // Suspend both, backdate suspension.
    mgr.update_score(1, {1, false, 0.9, 0.1, 5, 1, WorkerTier::Databot});
    mgr.update_score(2, {2, false, 0.8, 0.2, 4, 2, WorkerTier::Pipeline});
    auto old = clk::now() - std::chrono::milliseconds(500);
    mgr.set_suspended_since_for_test(1, old);
    mgr.set_suspended_since_for_test(2, old);

    auto decomm_databot = mgr.decommissionable_workers_by_tier(
        WorkerTier::Databot, std::chrono::milliseconds(100));
    CHECK(decomm_databot.size() == 1);
    CHECK(decomm_databot[0] == 1);

    auto decomm_pipeline = mgr.decommissionable_workers_by_tier(
        WorkerTier::Pipeline, std::chrono::milliseconds(100));
    CHECK(decomm_pipeline.size() == 1);
    CHECK(decomm_pipeline[0] == 2);

    auto decomm_agentic = mgr.decommissionable_workers_by_tier(
        WorkerTier::Agentic, std::chrono::milliseconds(100));
    CHECK(decomm_agentic.empty());
}

// --- Orchestrator tiered commission passes tier env var ---

TEST_CASE("Tiered orchestrator commissions Pipeline worker with correct env",
          "[elastic][tiered]") {
    InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1, WorkerTier::Databot});

    MockRuntime mock;
    Config cfg;
    cfg.elastic.enabled = true;
    cfg.elastic.min_workers = 1;
    cfg.elastic.max_workers = 10;
    cfg.elastic.pressure_threshold = 3;
    cfg.elastic.commission_cooldown_ms = 0;
    cfg.elastic.decommission_timeout_ms = 100;
    cfg.elastic.docker_image = "labios-worker";
    cfg.elastic.docker_network = "test-net";
    cfg.elastic.elastic_worker_speed = 3;
    cfg.elastic.elastic_worker_energy = 3;
    cfg.elastic.elastic_worker_capacity = "50GB";
    cfg.elastic.min_databot_workers = 1;
    cfg.elastic.max_databot_workers = 10;
    cfg.elastic.min_pipeline_workers = 0;
    cfg.elastic.max_pipeline_workers = 5;
    cfg.elastic.min_agentic_workers = 0;
    cfg.elastic.max_agentic_workers = 2;
    cfg.nats_url = "nats://localhost:4222";
    cfg.redis_host = "redis";
    cfg.dispatcher_batch_size = 100;

    Orchestrator<MockRuntime> orch(mgr, mock, cfg);

    // Simulate queue with pipeline demand but no pipeline workers.
    QueueBreakdown qb{100, 10, 0};
    for (int i = 0; i < 3; ++i) orch.update_queue_breakdown(qb);

    orch.evaluate_tiered_once();

    REQUIRE(mock.created.size() == 1);
    // Check that LABIOS_WORKER_TIER=1 is in the env.
    bool found_tier = false;
    for (const auto& e : mock.created[0].env) {
        if (e == "LABIOS_WORKER_TIER=1") { found_tier = true; break; }
    }
    CHECK(found_tier);
}

TEST_CASE("Tiered orchestrator commissions Databot when no SDS labels",
          "[elastic][tiered]") {
    InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1, WorkerTier::Databot});

    MockRuntime mock;
    Config cfg;
    cfg.elastic.enabled = true;
    cfg.elastic.pressure_threshold = 3;
    cfg.elastic.commission_cooldown_ms = 0;
    cfg.elastic.decommission_timeout_ms = 100;
    cfg.elastic.docker_image = "labios-worker";
    cfg.elastic.docker_network = "test-net";
    cfg.elastic.elastic_worker_speed = 3;
    cfg.elastic.elastic_worker_energy = 3;
    cfg.elastic.elastic_worker_capacity = "50GB";
    cfg.elastic.min_databot_workers = 1;
    cfg.elastic.max_databot_workers = 10;
    cfg.elastic.min_pipeline_workers = 0;
    cfg.elastic.max_pipeline_workers = 5;
    cfg.nats_url = "nats://localhost:4222";
    cfg.redis_host = "redis";
    cfg.dispatcher_batch_size = 100;

    Orchestrator<MockRuntime> orch(mgr, mock, cfg);

    // No pipeline labels.
    QueueBreakdown qb{100, 0, 0};
    for (int i = 0; i < 3; ++i) orch.update_queue_breakdown(qb);

    orch.evaluate_tiered_once();

    REQUIRE(mock.created.size() == 1);
    bool found_tier = false;
    for (const auto& e : mock.created[0].env) {
        if (e == "LABIOS_WORKER_TIER=0") { found_tier = true; break; }
    }
    CHECK(found_tier);
}
