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

TEST_CASE("update_score replaces worker info", "[worker_manager]") {
    labios::InMemoryWorkerManager mgr;
    mgr.register_worker({1, true, 0.9, 0.1, 5, 1});
    mgr.update_score(1, {1, true, 0.5, 0.5, 5, 1});

    auto all = mgr.all_workers();
    REQUIRE(all.size() == 1);
    CHECK(all[0].capacity == Catch::Approx(0.5));
    CHECK(all[0].load == Catch::Approx(0.5));
}
