#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/elastic/decision_engine.h>
#include <labios/label.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

namespace {

labios::LabelData make_checkpoint_label(int i) {
    labios::LabelData label;
    label.id = labios::generate_label_id(static_cast<uint32_t>(i % 256));
    label.type = labios::LabelType::Write;
    label.source = labios::file_path("/app/checkpoint_" + std::to_string(i) + ".ckpt");
    label.destination = labios::file_path("/pfs/checkpoints/step_" + std::to_string(i));
    label.operation = "checkpoint_write";
    label.intent = labios::Intent::Checkpoint;
    label.data_size = 64 * 1024; // 64KB per checkpoint
    label.priority = 10;
    label.durability = labios::Durability::Durable;
    return label;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: checkpoint labels serialize correctly in bulk
// ---------------------------------------------------------------------------

TEST_CASE("Checkpoint storm: 1000 labels all unique IDs", "[bench][checkpoint]") {
    std::vector<uint64_t> ids;
    ids.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        auto label = make_checkpoint_label(i);
        ids.push_back(label.id);
    }
    std::sort(ids.begin(), ids.end());
    auto it = std::unique(ids.begin(), ids.end());
    REQUIRE(it == ids.end());
}

TEST_CASE("Checkpoint storm: all labels roundtrip", "[bench][checkpoint]") {
    for (int i = 0; i < 100; ++i) {
        auto label = make_checkpoint_label(i);
        auto buf = labios::serialize_label(label);
        auto rt = labios::deserialize_label(buf);
        REQUIRE(rt.intent == labios::Intent::Checkpoint);
        REQUIRE(rt.durability == labios::Durability::Durable);
    }
}

TEST_CASE("Checkpoint storm: elastic engine under pressure", "[bench][checkpoint]") {
    using namespace labios::elastic;
    using clk = std::chrono::steady_clock;

    ElasticSnapshot snap{
        .pressure_count = 0,
        .pressure_threshold = 5,
        .current_workers = 2,
        .min_workers = 1,
        .max_workers = 20,
        .idle_worker_ids = {},
        .suspended_worker_ids = {},
        .last_commission = clk::now() - std::chrono::seconds(60),
        .cooldown = std::chrono::milliseconds(0),
    };

    int commissions = 0;
    for (int tick = 0; tick < 1000; ++tick) {
        snap.pressure_count = tick % 10; // oscillating pressure
        auto d = evaluate(snap);
        if (d.action == Action::Commission) {
            ++commissions;
            snap.current_workers++;
            snap.last_commission = clk::now();
        }
    }
    // With oscillating pressure and zero cooldown, expect some commissions
    CHECK(commissions > 0);
    CHECK(snap.current_workers <= snap.max_workers);
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Checkpoint storm benchmarks", "[bench][checkpoint][!benchmark]") {
    BENCHMARK("Checkpoint label creation 1000x") {
        std::vector<labios::LabelData> labels;
        labels.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            labels.push_back(make_checkpoint_label(i));
        }
        return labels.size();
    };

    BENCHMARK("Checkpoint serialize 1000x") {
        std::vector<std::vector<std::byte>> buffers;
        buffers.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            buffers.push_back(labios::serialize_label(make_checkpoint_label(i)));
        }
        return buffers.size();
    };

    using namespace labios::elastic;
    using clk = std::chrono::steady_clock;

    BENCHMARK("Elastic decision 1000 pressure events") {
        ElasticSnapshot snap{
            .pressure_count = 0,
            .pressure_threshold = 5,
            .current_workers = 2,
            .min_workers = 1,
            .max_workers = 100,
            .idle_worker_ids = {},
            .suspended_worker_ids = {},
            .last_commission = clk::now() - std::chrono::seconds(60),
            .cooldown = std::chrono::milliseconds(0),
        };

        int count = 0;
        for (int i = 0; i < 1000; ++i) {
            snap.pressure_count = i % 10;
            auto d = evaluate(snap);
            if (d.action != Action::None) ++count;
        }
        return count;
    };
}
