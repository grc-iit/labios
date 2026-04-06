#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/elastic/decision_engine.h>

#include <chrono>
#include <vector>

using namespace labios::elastic;
using clk = std::chrono::steady_clock;

namespace {

struct ScaleTrace {
    int commissions = 0;
    int decommissions = 0;
    int resumes = 0;
    int nones = 0;
};

ScaleTrace run_ramp_up(int ticks, int pressure_threshold) {
    ElasticSnapshot snap{
        .pressure_count = 0,
        .pressure_threshold = pressure_threshold,
        .current_workers = 2,
        .min_workers = 1,
        .max_workers = 50,
        .idle_worker_ids = {},
        .suspended_worker_ids = {},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::milliseconds(0),
    };

    ScaleTrace trace;
    for (int t = 0; t < ticks; ++t) {
        snap.pressure_count = (t * 100) / ticks; // linear ramp 0 -> 100
        auto d = evaluate(snap);
        switch (d.action) {
            case Action::Commission:
                ++trace.commissions;
                snap.current_workers++;
                snap.last_commission = clk::now();
                break;
            case Action::Decommission:
                ++trace.decommissions;
                if (snap.current_workers > snap.min_workers) snap.current_workers--;
                break;
            case Action::Resume:
                ++trace.resumes;
                break;
            case Action::None:
                ++trace.nones;
                break;
        }
    }
    return trace;
}

ScaleTrace run_ramp_down(int ticks, int pressure_threshold) {
    ElasticSnapshot snap{
        .pressure_count = 100,
        .pressure_threshold = pressure_threshold,
        .current_workers = 20,
        .min_workers = 1,
        .max_workers = 50,
        .idle_worker_ids = {},
        .suspended_worker_ids = {},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::milliseconds(0),
    };

    ScaleTrace trace;
    for (int t = 0; t <= ticks; ++t) {
        snap.pressure_count = 100 - (t * 100) / ticks; // linear ramp 100 -> 0

        // Decommission only triggers when pressure_count == 0 and idle workers exist
        if (snap.pressure_count == 0 && snap.current_workers > snap.min_workers) {
            snap.idle_worker_ids = {snap.current_workers};
        } else {
            snap.idle_worker_ids.clear();
        }

        auto d = evaluate(snap);
        switch (d.action) {
            case Action::Commission:
                ++trace.commissions;
                snap.current_workers++;
                snap.last_commission = clk::now();
                break;
            case Action::Decommission:
                ++trace.decommissions;
                if (snap.current_workers > snap.min_workers) snap.current_workers--;
                snap.idle_worker_ids.clear();
                break;
            case Action::Resume:
                ++trace.resumes;
                break;
            case Action::None:
                ++trace.nones;
                break;
        }
    }
    return trace;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: scaling behavior under ramp patterns
// ---------------------------------------------------------------------------

TEST_CASE("Scale adaptation: ramp up produces commissions", "[bench][scale]") {
    auto trace = run_ramp_up(100, 5);
    CHECK(trace.commissions > 0);
    // Should not decommission while ramping up
    CHECK(trace.decommissions == 0);
}

TEST_CASE("Scale adaptation: ramp down produces decommissions", "[bench][scale]") {
    auto trace = run_ramp_down(100, 5);
    CHECK(trace.decommissions > 0);
}

TEST_CASE("Scale adaptation: high threshold reduces commission count", "[bench][scale]") {
    // Use max_workers=200 so the cap doesn't mask the threshold effect
    auto run_with_threshold = [](int threshold) {
        ElasticSnapshot snap{
            .pressure_count = 0,
            .pressure_threshold = threshold,
            .current_workers = 2,
            .min_workers = 1,
            .max_workers = 200,
            .idle_worker_ids = {},
            .suspended_worker_ids = {},
            .last_commission = clk::now() - std::chrono::seconds(60),
            .cooldown = std::chrono::milliseconds(0),
        };
        int commissions = 0;
        for (int t = 0; t < 100; ++t) {
            snap.pressure_count = (t * 100) / 100;
            auto d = evaluate(snap);
            if (d.action == Action::Commission) {
                ++commissions;
                snap.current_workers++;
                snap.last_commission = clk::now();
            }
        }
        return commissions;
    };

    int low = run_with_threshold(5);
    int high = run_with_threshold(50);
    CHECK(high < low);
}

TEST_CASE("Scale adaptation: tiered ramp up targets correct tiers", "[bench][scale]") {
    TieredSnapshot snap{
        .queue = {0, 0, 0},
        .pressure_count = 0,
        .pressure_threshold = 5,
        .databot = {2, 1, 20, {}, {}},
        .pipeline = {0, 0, 10, {}, {}},
        .agentic = {0, 0, 5, {}, {}},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::milliseconds(0),
    };

    int databot_commissions = 0;
    int pipeline_commissions = 0;

    for (int t = 0; t < 100; ++t) {
        snap.pressure_count = (t * 100) / 100;
        snap.queue.total = snap.pressure_count * 10;
        // Half the ticks have pipeline demand
        snap.queue.with_pipeline = (t % 2 == 0) ? snap.queue.total / 2 : 0;

        auto d = evaluate_tiered(snap);
        if (d.action == Action::Commission) {
            if (d.target_tier == labios::WorkerTier::Databot) {
                ++databot_commissions;
                snap.databot.active++;
            } else if (d.target_tier == labios::WorkerTier::Pipeline) {
                ++pipeline_commissions;
                snap.pipeline.active++;
            }
            snap.last_commission = clk::now();
        }
    }

    CHECK(databot_commissions > 0);
    // Pipeline commissions should happen when SDS demand exists and no pipeline workers
    CHECK(pipeline_commissions > 0);
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Scale adaptation benchmarks", "[bench][scale][!benchmark]") {
    BENCHMARK("Scale-up 0->100 pressure (100 ticks)") {
        return run_ramp_up(100, 5);
    };

    BENCHMARK("Scale-down 100->0 pressure (100 ticks)") {
        return run_ramp_down(100, 5);
    };

    BENCHMARK("Tiered scale-up 100 ticks") {
        TieredSnapshot snap{
            .queue = {0, 0, 0},
            .pressure_count = 0,
            .pressure_threshold = 5,
            .databot = {2, 1, 50, {}, {}},
            .pipeline = {0, 0, 20, {}, {}},
            .agentic = {0, 0, 5, {}, {}},
            .last_commission = clk::now() - std::chrono::seconds(60),
            .cooldown = std::chrono::milliseconds(0),
        };

        int decisions = 0;
        for (int t = 0; t < 100; ++t) {
            snap.pressure_count = (t * 100) / 100;
            snap.queue.total = snap.pressure_count * 10;
            snap.queue.with_pipeline = snap.queue.total / 3;

            auto d = evaluate_tiered(snap);
            if (d.action != Action::None) {
                ++decisions;
                if (d.target_tier == labios::WorkerTier::Databot)
                    snap.databot.active++;
                else if (d.target_tier == labios::WorkerTier::Pipeline)
                    snap.pipeline.active++;
                snap.last_commission = clk::now();
            }
        }
        return decisions;
    };

    BENCHMARK("Evaluate decision 10000x (steady state)") {
        ElasticSnapshot snap{
            .pressure_count = 3,
            .pressure_threshold = 5,
            .current_workers = 5,
            .min_workers = 1,
            .max_workers = 20,
            .idle_worker_ids = {},
            .suspended_worker_ids = {},
            .last_commission = clk::now() - std::chrono::seconds(60),
            .cooldown = std::chrono::milliseconds(0),
        };

        int nones = 0;
        for (int i = 0; i < 10000; ++i) {
            auto d = evaluate(snap);
            if (d.action == Action::None) ++nones;
        }
        return nones;
    };
}
