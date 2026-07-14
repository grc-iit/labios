#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/label.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

labios::LabelData make_label(uint64_t data_size, uint32_t app_id = 1) {
    labios::LabelData label;
    label.id = labios::generate_label_id(app_id);
    label.type = labios::LabelType::Write;
    label.source = labios::file_path("/agent/scratch/input.dat", 0, data_size);
    label.destination = labios::file_path("/agent/scratch/output.dat");
    label.operation = "write_block";
    label.flags = labios::LabelFlags::Queued;
    label.priority = 3;
    label.app_id = app_id;
    label.data_size = data_size;
    label.intent = labios::Intent::ToolOutput;
    label.isolation = labios::Isolation::Agent;
    label.source_uri = "file:///agent/scratch/input.dat";
    label.dest_uri = "file:///agent/scratch/output.dat";
    return label;
}

struct AgentTraceItem {
    uint64_t size;
    bool read_after_write;
};

// A deterministic, representative coding-agent control-plane trace. The
// payloads are represented only by their sizes: this is not execution I/O.
std::vector<AgentTraceItem> coding_agent_trace() {
    std::mt19937 rng(20260714);
    std::uniform_int_distribution<int> small(512, 4096);
    std::vector<AgentTraceItem> trace;
    trace.reserve(128);
    for (int i = 0; i < 96; ++i) trace.push_back({static_cast<uint64_t>(small(rng)), i % 8 == 0});
    for (int i = 0; i < 20; ++i) trace.push_back({static_cast<uint64_t>(8 * 1024 + i * 1024), i % 5 == 0});
    for (int i = 0; i < 8; ++i) trace.push_back({256 * 1024 + static_cast<uint64_t>(i) * 4096, i == 0});
    for (int i = 0; i < 4; ++i) trace.push_back({2 * 1024 * 1024 + static_cast<uint64_t>(i) * 64 * 1024, true});
    return trace;
}

} // anonymous namespace

// These tests measure label mechanics only; no dispatcher, worker, or backend
// is contacted and they do not establish end-to-end coding-agent I/O value.
TEST_CASE("Control-plane label encoding: 1KB roundtrip", "[bench][coding_agent][control_plane]") {
    auto label = make_label(1024);
    auto buf = labios::serialize_label(label);
    REQUIRE(!buf.empty());
    auto rt = labios::deserialize_label(buf);
    REQUIRE(rt.id == label.id);
    REQUIRE(rt.data_size == 1024);
    REQUIRE(rt.intent == labios::Intent::ToolOutput);
}

TEST_CASE("Control-plane label encoding: 1MB roundtrip", "[bench][coding_agent][control_plane]") {
    auto label = make_label(1024 * 1024);
    auto buf = labios::serialize_label(label);
    auto rt = labios::deserialize_label(buf);
    REQUIRE(rt.data_size == 1024 * 1024);
}

TEST_CASE("Control-plane label ID generation: 1000 unique IDs", "[bench][coding_agent][control_plane]") {
    std::vector<uint64_t> ids;
    ids.reserve(1000);
    for (int i = 0; i < 1000; ++i) ids.push_back(labios::generate_label_id(1));
    std::sort(ids.begin(), ids.end());
    REQUIRE(std::unique(ids.begin(), ids.end()) == ids.end());
}

TEST_CASE("Coding-agent trace model has deterministic mixed I/O distribution", "[bench][coding_agent][control_plane]") {
    const auto trace = coding_agent_trace();
    REQUIRE(trace.size() == 128);
    REQUIRE(std::count_if(trace.begin(), trace.end(), [](const auto& item) { return item.size <= 4096; }) == 96);
    REQUIRE(std::count_if(trace.begin(), trace.end(), [](const auto& item) { return item.size >= 2 * 1024 * 1024; }) == 4);
    REQUIRE(std::count_if(trace.begin(), trace.end(), [](const auto& item) { return item.read_after_write; }) > 0);
}

TEST_CASE("Coding-agent control-plane microbenchmarks", "[bench][coding_agent][control_plane][!benchmark]") {
    BENCHMARK("Control-plane label construction (1KB)") { return make_label(1024); };
    BENCHMARK("Control-plane label construction (1MB)") { return make_label(1024 * 1024); };

    const auto trace = coding_agent_trace();
    BENCHMARK("Control-plane coding-agent trace: construct and encode labels") {
        size_t encoded_bytes = 0;
        for (const auto& item : trace) encoded_bytes += labios::serialize_label(make_label(item.size)).size();
        return encoded_bytes;
    };

    auto label_1k = make_label(1024);
    auto buf_1k = labios::serialize_label(label_1k);
    BENCHMARK("Label encoding microbenchmark: serialize 1KB") { return labios::serialize_label(label_1k); };
    BENCHMARK("Label encoding microbenchmark: deserialize 1KB") { return labios::deserialize_label(buf_1k); };

    auto label_1m = make_label(1024 * 1024);
    auto buf_1m = labios::serialize_label(label_1m);
    BENCHMARK("Label encoding microbenchmark: serialize 1MB") { return labios::serialize_label(label_1m); };
    BENCHMARK("Label encoding microbenchmark: deserialize 1MB") { return labios::deserialize_label(buf_1m); };

    BENCHMARK("Control-plane ID generation microbenchmark: 1000 labels") {
        uint64_t last = 0;
        for (int i = 0; i < 1000; ++i) last = labios::generate_label_id(1);
        return last;
    };
}
