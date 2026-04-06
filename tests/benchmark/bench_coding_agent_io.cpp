#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/label.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
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

std::vector<std::byte> make_data(size_t size) {
    std::vector<std::byte> data(size);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + size,
              static_cast<uint8_t>(0));
    return data;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: labels roundtrip at various sizes
// ---------------------------------------------------------------------------

TEST_CASE("Coding agent: label roundtrip 1KB", "[bench][coding_agent]") {
    auto label = make_label(1024);
    auto buf = labios::serialize_label(label);
    REQUIRE(!buf.empty());
    auto rt = labios::deserialize_label(buf);
    REQUIRE(rt.id == label.id);
    REQUIRE(rt.data_size == 1024);
    REQUIRE(rt.intent == labios::Intent::ToolOutput);
}

TEST_CASE("Coding agent: label roundtrip 1MB", "[bench][coding_agent]") {
    auto label = make_label(1024 * 1024);
    auto buf = labios::serialize_label(label);
    auto rt = labios::deserialize_label(buf);
    REQUIRE(rt.data_size == 1024 * 1024);
}

TEST_CASE("Coding agent: 1000 unique label IDs", "[bench][coding_agent]") {
    std::vector<uint64_t> ids;
    ids.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        ids.push_back(labios::generate_label_id(1));
    }
    std::sort(ids.begin(), ids.end());
    auto it = std::unique(ids.begin(), ids.end());
    REQUIRE(it == ids.end());
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Coding agent benchmarks", "[bench][coding_agent][!benchmark]") {
    BENCHMARK("Label creation 1KB") {
        return make_label(1024);
    };

    BENCHMARK("Label creation 1MB") {
        return make_label(1024 * 1024);
    };

    auto label_1k = make_label(1024);
    BENCHMARK("Serialize 1KB") {
        return labios::serialize_label(label_1k);
    };

    auto buf_1k = labios::serialize_label(label_1k);
    BENCHMARK("Deserialize 1KB") {
        return labios::deserialize_label(buf_1k);
    };

    auto label_1m = make_label(1024 * 1024);
    BENCHMARK("Serialize 1MB") {
        return labios::serialize_label(label_1m);
    };

    auto buf_1m = labios::serialize_label(label_1m);
    BENCHMARK("Deserialize 1MB") {
        return labios::deserialize_label(buf_1m);
    };

    BENCHMARK("Baseline: vector allocation 1KB") {
        return make_data(1024);
    };

    BENCHMARK("Baseline: vector allocation 1MB") {
        return make_data(1024 * 1024);
    };

    BENCHMARK("ID generation 1000x") {
        uint64_t last = 0;
        for (int i = 0; i < 1000; ++i) {
            last = labios::generate_label_id(1);
        }
        return last;
    };
}
