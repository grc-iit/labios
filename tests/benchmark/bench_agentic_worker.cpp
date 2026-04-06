#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/label.h>
#include <labios/solver/solver.h>
#include <labios/worker_manager.h>

#include <vector>

namespace {

void populate_workers(labios::InMemoryWorkerManager& mgr) {
    int id = 1;
    // 60 Databot workers
    for (int i = 0; i < 60; ++i, ++id) {
        mgr.register_worker({id, true,
            0.8 - 0.005 * i,   // capacity varies
            0.1 + 0.005 * i,   // load varies
            (i % 5) + 1,       // speed 1-5
            (i % 5) + 1,       // energy 1-5
            labios::WorkerTier::Databot});
    }
    // 30 Pipeline workers
    for (int i = 0; i < 30; ++i, ++id) {
        mgr.register_worker({id, true,
            0.7 - 0.005 * i,
            0.2 + 0.005 * i,
            (i % 5) + 1,
            (i % 5) + 1,
            labios::WorkerTier::Pipeline});
    }
    // 10 Agentic workers
    for (int i = 0; i < 10; ++i, ++id) {
        mgr.register_worker({id, true,
            0.6 - 0.01 * i,
            0.3 + 0.01 * i,
            (i % 5) + 1,
            (i % 5) + 1,
            labios::WorkerTier::Agentic});
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: tier gating and scoring
// ---------------------------------------------------------------------------

TEST_CASE("Agentic worker: 100 workers registered across tiers", "[bench][agentic]") {
    labios::InMemoryWorkerManager mgr;
    populate_workers(mgr);

    CHECK(mgr.worker_count() == 100);
    CHECK(mgr.count_by_tier(labios::WorkerTier::Databot) == 60);
    CHECK(mgr.count_by_tier(labios::WorkerTier::Pipeline) == 30);
    CHECK(mgr.count_by_tier(labios::WorkerTier::Agentic) == 10);
}

TEST_CASE("Agentic worker: agentic profile ranks Agentic workers highest",
          "[bench][agentic]") {
    labios::InMemoryWorkerManager mgr;
    populate_workers(mgr);

    labios::WeightProfile agentic_wp{"agentic", 0.3, 0.1, 0.1, 0.1, 0.0, 0.4};
    auto top = mgr.top_n_workers(5, agentic_wp);
    REQUIRE(top.size() == 5);

    // All top-5 should be Agentic tier under this profile
    for (const auto& w : top) {
        CHECK(w.tier == labios::WorkerTier::Agentic);
    }
}

TEST_CASE("Agentic worker: low_latency profile ignores tier", "[bench][agentic]") {
    labios::InMemoryWorkerManager mgr;
    populate_workers(mgr);

    labios::WeightProfile low_lat{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0, 0.0};
    auto top = mgr.top_n_workers(10, low_lat);
    REQUIRE(top.size() == 10);

    // With tier weight = 0, top workers are based on availability/load/speed only
    // First workers have best capacity and lowest load, so they should appear
    CHECK(top[0].id <= 60); // Likely from early Databot workers
}

TEST_CASE("Agentic worker: tier gating rejects pipelines on Databot", "[bench][agentic]") {
    // Labels with pipeline require tier >= Pipeline (1)
    labios::LabelData label;
    label.pipeline.stages.push_back({"builtin://identity", "", -1, -1});
    bool has_pipeline = !label.pipeline.empty();
    REQUIRE(has_pipeline);

    // A Databot worker (tier 0) cannot handle this
    labios::WorkerInfo databot{1, true, 0.9, 0.0, 5, 1, labios::WorkerTier::Databot};
    CHECK(static_cast<int>(databot.tier) < 1);

    // A Pipeline worker (tier 1) can
    labios::WorkerInfo pipeline_w{2, true, 0.9, 0.0, 5, 1, labios::WorkerTier::Pipeline};
    CHECK(static_cast<int>(pipeline_w.tier) >= 1);
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Agentic worker benchmarks", "[bench][agentic][!benchmark]") {
    labios::InMemoryWorkerManager mgr;
    populate_workers(mgr);

    labios::WeightProfile low_lat{"low_latency", 0.5, 0.0, 0.35, 0.15, 0.0, 0.0};
    labios::WeightProfile agentic_wp{"agentic", 0.3, 0.1, 0.1, 0.1, 0.0, 0.4};

    BENCHMARK("Score 100 workers (low_latency)") {
        auto all = mgr.all_workers();
        double total = 0.0;
        for (auto& w : all) {
            total += labios::compute_score(w, low_lat);
        }
        return total;
    };

    BENCHMARK("Score 100 workers (agentic)") {
        auto all = mgr.all_workers();
        double total = 0.0;
        for (auto& w : all) {
            total += labios::compute_score(w, agentic_wp);
        }
        return total;
    };

    BENCHMARK("Top-10 query (low_latency)") {
        return mgr.top_n_workers(10, low_lat);
    };

    BENCHMARK("Top-10 query (agentic)") {
        return mgr.top_n_workers(10, agentic_wp);
    };

    BENCHMARK("Tier gate check 1000 labels") {
        int rejected = 0;
        for (int i = 0; i < 1000; ++i) {
            labios::LabelData label;
            if (i % 3 == 0) {
                label.pipeline.stages.push_back({"builtin://identity", "", -1, -1});
            }
            bool needs_pipeline = !label.pipeline.empty();
            labios::WorkerTier required_tier = needs_pipeline
                ? labios::WorkerTier::Pipeline
                : labios::WorkerTier::Databot;
            if (static_cast<int>(labios::WorkerTier::Databot) < static_cast<int>(required_tier)) {
                ++rejected;
            }
        }
        return rejected;
    };
}
