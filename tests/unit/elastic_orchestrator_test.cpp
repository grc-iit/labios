#include <labios/elastic/orchestrator.h>
#include <labios/elastic/docker_client.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Orchestrator commissions on sustained pressure", "[elastic]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.5, 5, 1});

    labios::elastic::MockRuntime mock;
    labios::Config cfg;
    cfg.elastic.enabled = true;
    cfg.elastic.min_workers = 1;
    cfg.elastic.max_workers = 3;
    cfg.elastic.pressure_threshold = 3;
    cfg.elastic.commission_cooldown_ms = 0;
    cfg.elastic.eval_interval_ms = 50;
    cfg.elastic.docker_image = "labios-worker";
    cfg.elastic.docker_network = "test-net";
    cfg.elastic.elastic_worker_speed = 3;
    cfg.elastic.elastic_worker_energy = 3;
    cfg.elastic.elastic_worker_capacity = "50GB";
    cfg.nats_url = "nats://localhost:4222";
    cfg.redis_host = "redis";
    cfg.dispatcher_batch_size = 100;

    labios::elastic::Orchestrator<labios::elastic::MockRuntime> orch(mgr, mock, cfg);

    // Simulate 3 consecutive full batches.
    orch.update_queue_depth(100);
    orch.update_queue_depth(100);
    orch.update_queue_depth(100);

    orch.evaluate_once();

    CHECK(mock.created.size() == 1);
    CHECK(mock.created[0].image == "labios-worker");
    CHECK(mock.created[0].network == "test-net");
}

TEST_CASE("Orchestrator decommissions idle elastic worker", "[elastic]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.0, 5, 1});
    mgr.register_worker({100, true, 0.9, 0.0, 3, 3});

    labios::elastic::MockRuntime mock;
    labios::Config cfg;
    cfg.elastic.enabled = true;
    cfg.elastic.min_workers = 1;
    cfg.elastic.max_workers = 3;
    cfg.elastic.pressure_threshold = 5;
    cfg.elastic.decommission_timeout_ms = 100;
    cfg.elastic.eval_interval_ms = 50;
    cfg.elastic.elastic_worker_speed = 3;
    cfg.elastic.elastic_worker_energy = 3;
    cfg.elastic.elastic_worker_capacity = "50GB";
    cfg.nats_url = "nats://localhost:4222";
    cfg.redis_host = "redis";
    cfg.dispatcher_batch_size = 100;

    labios::elastic::Orchestrator<labios::elastic::MockRuntime> orch(mgr, mock, cfg);

    // Register worker 100 as commissioned by the orchestrator.
    orch.record_commissioned(100, "mock-container-0");

    // Simulate worker 100 self-suspending.
    mgr.update_score(100, {100, false, 0.9, 0.0, 3, 3});

    // Backdate suspension to exceed timeout.
    mgr.set_suspended_since_for_test(100,
        std::chrono::steady_clock::now() - std::chrono::milliseconds(200));

    // No queue pressure.
    orch.update_queue_depth(0);

    orch.evaluate_once();

    CHECK(mock.stopped.size() == 1);
    CHECK(mock.stopped[0] == "mock-container-0");
}

TEST_CASE("Orchestrator does not decommission static workers", "[elastic]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.0, 5, 1});

    labios::elastic::MockRuntime mock;
    labios::Config cfg;
    cfg.elastic.enabled = true;
    cfg.elastic.min_workers = 1;
    cfg.elastic.max_workers = 3;
    cfg.elastic.decommission_timeout_ms = 100;
    cfg.elastic.elastic_worker_speed = 3;
    cfg.elastic.elastic_worker_energy = 3;
    cfg.elastic.elastic_worker_capacity = "50GB";
    cfg.nats_url = "nats://localhost:4222";
    cfg.redis_host = "redis";
    cfg.dispatcher_batch_size = 100;

    labios::elastic::Orchestrator<labios::elastic::MockRuntime> orch(mgr, mock, cfg);

    // Worker 1 is static (not in commissioned map). Suspend it.
    mgr.update_score(1, {1, false, 0.9, 0.0, 5, 1});
    mgr.set_suspended_since_for_test(1,
        std::chrono::steady_clock::now() - std::chrono::milliseconds(200));

    orch.update_queue_depth(0);
    orch.evaluate_once();

    CHECK(mock.stopped.empty());
}
