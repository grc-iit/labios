#include <labios/worker_manager.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

TEST_CASE("Worker score computed from weight profile", "[worker_manager]") {
    labios::WeightProfile wp{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0};
    labios::WorkerInfo w{1, true, 0.8, 0.3, 5, 2};

    double score = labios::compute_score(w, wp);
    // 0.5*1.0 + 0.0*0.8 + 0.35*(1.0-0.3) + 0.15*(5.0/5.0) + 0.0*(2.0/5.0)
    // = 0.5 + 0.0 + 0.245 + 0.15 + 0.0 = 0.895
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
