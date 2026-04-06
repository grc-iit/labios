#include <labios/worker_manager.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>

using Catch::Matchers::WithinAbs;

TEST_CASE("Worker score computed from weight profile", "[worker_manager]") {
    labios::WeightProfile wp{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0, 0.0};
    labios::WorkerInfo w{1, true, 0.8, 0.3, 5, 2};

    double score = labios::compute_score(w, wp);
    // 0.5*1.0 + 0.0*0.8 + 0.35*(1.0-0.3) + 0.15*(5.0/5.0) + 0.0*(2.0/5.0) + 0.0*0
    // = 0.5 + 0.0 + 0.245 + 0.15 + 0.0 + 0.0 = 0.895
    CHECK_THAT(score, WithinAbs(0.895, 0.001));
}

TEST_CASE("Unavailable worker scores zero on availability", "[worker_manager]") {
    labios::WeightProfile wp{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0};
    labios::WorkerInfo w{1, false, 0.8, 0.3, 5, 2};

    double score = labios::compute_score(w, wp);
    // availability = 0, so 0.5*0 = 0 for that term
    CHECK_THAT(score, WithinAbs(0.395, 0.001));
}

TEST_CASE("InMemoryWorkerManager registers and retrieves workers", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    labios::WorkerInfo w1{1, true, 0.9, 0.1, 5, 1};
    labios::WorkerInfo w2{2, true, 0.5, 0.5, 3, 3};
    labios::WorkerInfo w3{3, true, 0.2, 0.8, 1, 5};

    mgr.register_worker(w1);
    mgr.register_worker(w2);
    mgr.register_worker(w3);

    auto all = mgr.all_workers();
    CHECK(all.size() == 3);
}

TEST_CASE("top_n_workers returns workers sorted by score descending", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1});
    mgr.register_worker({2, true, 0.5, 0.5, 3, 3});
    mgr.register_worker({3, true, 0.2, 0.8, 1, 5});

    labios::WeightProfile wp{"high_bandwidth", 0.0, 0.15, 0.15, 0.70, 0.0};
    auto top = mgr.top_n_workers(2, wp);
    CHECK(top.size() == 2);
    // Worker 1 (speed=5) should rank highest under high_bandwidth profile.
    CHECK(top[0].id == 1);
}

TEST_CASE("Deregistered workers are not returned", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true});
    mgr.register_worker({2, true});
    mgr.deregister_worker(1);

    auto all = mgr.all_workers();
    CHECK(all.size() == 1);
    CHECK(all[0].id == 2);
}

TEST_CASE("Bucket-sorted top_n picks highest-scored workers from 12", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    // Register 12 workers with varying speed (dominant factor under high_bandwidth).
    for (int i = 1; i <= 12; ++i) {
        int speed = (i % 5) + 1;  // speeds cycle 2,3,4,5,1,2,3,4,5,1,2,3
        mgr.register_worker({i, true, 0.8, 0.1, speed, 2});
    }

    labios::WeightProfile wp{"high_bandwidth", 0.0, 0.15, 0.15, 0.70, 0.0};
    auto top3 = mgr.top_n_workers(3, wp);
    REQUIRE(top3.size() == 3);

    // All returned workers should have speed >= 4 (the top bucket).
    for (auto& w : top3) {
        CHECK(w.speed >= 4);
    }
}

TEST_CASE("Bucket placement updates on score change", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    for (int i = 1; i <= 10; ++i) {
        mgr.register_worker({i, true, 0.5, 0.5, i <= 5 ? 1 : 5, 2});
    }

    labios::WeightProfile wp{"high_bandwidth", 0.0, 0.15, 0.15, 0.70, 0.0};
    // Initialize buckets.
    auto initial = mgr.top_n_workers(5, wp);
    REQUIRE(initial.size() == 5);

    // Boost worker 1 (was speed=1) to speed=5. It should now appear in top results.
    mgr.update_score(1, {1, true, 0.9, 0.0, 5, 1});
    auto updated = mgr.top_n_workers(5, wp);
    bool found = false;
    for (auto& w : updated) {
        if (w.id == 1) { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("update_score replaces worker info", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1});
    mgr.update_score(1, {1, true, 0.5, 0.5, 5, 1});

    auto all = mgr.all_workers();
    REQUIRE(all.size() == 1);
    CHECK(all[0].capacity == Catch::Approx(0.5));
    CHECK(all[0].load == Catch::Approx(0.5));
}

TEST_CASE("worker_count returns registered count", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    CHECK(mgr.worker_count() == 0);
    mgr.register_worker({1, true});
    mgr.register_worker({2, true});
    CHECK(mgr.worker_count() == 2);
    mgr.deregister_worker(1);
    CHECK(mgr.worker_count() == 1);
}

TEST_CASE("next_worker_id starts at 100 and increments", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true});
    mgr.register_worker({2, true});
    CHECK(mgr.next_worker_id() == 100);
    CHECK(mgr.next_worker_id() == 101);
    CHECK(mgr.next_worker_id() == 102);
}

TEST_CASE("suspended_workers returns workers with available=false", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.0, 5, 1});
    mgr.register_worker({2, true, 0.5, 0.0, 3, 3});

    CHECK(mgr.suspended_workers().empty());

    // Simulate worker 2 self-suspending via score update.
    mgr.update_score(2, {2, false, 0.5, 0.0, 3, 3});

    auto susp = mgr.suspended_workers();
    REQUIRE(susp.size() == 1);
    CHECK(susp[0] == 2);
}

TEST_CASE("decommissionable_workers returns workers suspended beyond timeout", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.0, 5, 1});

    // Simulate suspension.
    mgr.update_score(1, {1, false, 0.9, 0.0, 5, 1});

    // Just suspended: not decommissionable at 1-hour threshold.
    auto decomm = mgr.decommissionable_workers(std::chrono::hours(1));
    CHECK(decomm.empty());

    // Backdate the suspension timestamp for testing.
    mgr.set_suspended_since_for_test(1,
        std::chrono::steady_clock::now() - std::chrono::seconds(120));

    decomm = mgr.decommissionable_workers(std::chrono::seconds(60));
    REQUIRE(decomm.size() == 1);
    CHECK(decomm[0] == 1);
}

TEST_CASE("Resuming worker clears suspension state", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.0, 5, 1});

    // Suspend.
    mgr.update_score(1, {1, false, 0.9, 0.0, 5, 1});
    CHECK(mgr.suspended_workers().size() == 1);

    // Resume: worker reports available=true again.
    mgr.update_score(1, {1, true, 0.9, 0.5, 5, 1});
    CHECK(mgr.suspended_workers().empty());
}

TEST_CASE("Tier weight contributes to compute_score", "[worker_manager]") {
    labios::WeightProfile wp{"agentic", 0.3, 0.1, 0.1, 0.1, 0.0, 0.4};

    labios::WorkerInfo databot{1, true, 0.5, 0.2, 3, 2, labios::WorkerTier::Databot};
    labios::WorkerInfo agentic{2, true, 0.5, 0.2, 3, 2, labios::WorkerTier::Agentic};

    double score_d = labios::compute_score(databot, wp);
    double score_a = labios::compute_score(agentic, wp);

    // Identical stats except tier. Agentic (tier=2) should score higher.
    // Tier contribution: databot = 0.4*(0/2) = 0.0, agentic = 0.4*(2/2) = 0.4
    CHECK(score_a > score_d);
    CHECK_THAT(score_a - score_d, WithinAbs(0.4, 0.001));
}

TEST_CASE("Zero tier weight preserves existing scoring behavior", "[worker_manager]") {
    labios::WeightProfile wp{"high_bandwidth", 0.0, 0.15, 0.15, 0.70, 0.0, 0.0};

    labios::WorkerInfo databot{1, true, 0.8, 0.1, 5, 1, labios::WorkerTier::Databot};
    labios::WorkerInfo agentic{2, true, 0.8, 0.1, 5, 1, labios::WorkerTier::Agentic};

    double score_d = labios::compute_score(databot, wp);
    double score_a = labios::compute_score(agentic, wp);

    // With tier weight = 0.0, tier difference has no effect.
    CHECK_THAT(score_d, WithinAbs(score_a, 0.001));
}

TEST_CASE("Pipeline tier scores between Databot and Agentic", "[worker_manager]") {
    labios::WeightProfile wp{"tier_only", 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};

    labios::WorkerInfo d{1, true, 0.5, 0.5, 3, 3, labios::WorkerTier::Databot};
    labios::WorkerInfo p{2, true, 0.5, 0.5, 3, 3, labios::WorkerTier::Pipeline};
    labios::WorkerInfo a{3, true, 0.5, 0.5, 3, 3, labios::WorkerTier::Agentic};

    double sd = labios::compute_score(d, wp);
    double sp = labios::compute_score(p, wp);
    double sa = labios::compute_score(a, wp);

    CHECK_THAT(sd, WithinAbs(0.0, 0.001));
    CHECK_THAT(sp, WithinAbs(0.5, 0.001));
    CHECK_THAT(sa, WithinAbs(1.0, 0.001));
    CHECK(sd < sp);
    CHECK(sp < sa);
}

TEST_CASE("top_n_workers ranks Agentic workers first under agentic profile", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.8, 0.1, 3, 2, labios::WorkerTier::Databot});
    mgr.register_worker({2, true, 0.8, 0.1, 3, 2, labios::WorkerTier::Agentic});
    mgr.register_worker({3, true, 0.8, 0.1, 3, 2, labios::WorkerTier::Pipeline});

    labios::WeightProfile wp{"agentic", 0.3, 0.1, 0.1, 0.1, 0.0, 0.4};
    auto top = mgr.top_n_workers(1, wp);
    REQUIRE(top.size() == 1);
    CHECK(top[0].id == 2);
}

TEST_CASE("WorkerInfo tier defaults to Databot", "[worker_manager]") {
    labios::WorkerInfo w{1, true, 0.5, 0.0, 3, 2};
    CHECK(w.tier == labios::WorkerTier::Databot);
}

TEST_CASE("WorkerInfo tier round-trips through register/all_workers", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.0, 5, 1, labios::WorkerTier::Pipeline});
    mgr.register_worker({2, true, 0.8, 0.0, 3, 2, labios::WorkerTier::Agentic});

    auto all = mgr.all_workers();
    REQUIRE(all.size() == 2);

    for (auto& w : all) {
        if (w.id == 1) CHECK(w.tier == labios::WorkerTier::Pipeline);
        if (w.id == 2) CHECK(w.tier == labios::WorkerTier::Agentic);
    }
}
