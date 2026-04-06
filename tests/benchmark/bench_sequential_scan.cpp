#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/label.h>
#include <labios/sds/executor.h>
#include <labios/sds/program_repo.h>
#include <labios/sds/types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int FILE_COUNT = 500;
constexpr size_t FILE_SIZE = 1024; // 1KB each
constexpr std::byte NEEDLE{0x42};

/// Create FILE_COUNT files under dir, each FILE_SIZE bytes.
/// Every 10th file contains the NEEDLE byte pattern (simulating grep hits).
void create_test_files(const fs::path& dir) {
    fs::create_directories(dir);
    for (int i = 0; i < FILE_COUNT; ++i) {
        auto path = dir / ("file_" + std::to_string(i) + ".dat");
        std::vector<std::byte> data(FILE_SIZE);
        // Fill with a byte that is never NEEDLE, derived from file index
        auto fill = static_cast<uint8_t>((i % 200) + 1);
        if (static_cast<std::byte>(fill) == NEEDLE) fill++;
        std::fill(reinterpret_cast<uint8_t*>(data.data()),
                  reinterpret_cast<uint8_t*>(data.data()) + FILE_SIZE,
                  fill);
        // Inject needle into every 10th file
        if (i % 10 == 0) {
            std::fill(data.begin(), data.begin() + 16, NEEDLE);
        }
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
}

void cleanup_test_files(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

struct ScanResult {
    int files_scanned = 0;
    int matches = 0;
    uint64_t bytes_read = 0;
};

/// Vanilla: open each file, read contents, check for needle byte.
ScanResult vanilla_sequential_scan(const fs::path& dir) {
    ScanResult result;
    for (int i = 0; i < FILE_COUNT; ++i) {
        auto path = dir / ("file_" + std::to_string(i) + ".dat");
        std::ifstream ifs(path, std::ios::binary);
        std::vector<char> buf(FILE_SIZE);
        ifs.read(buf.data(), static_cast<std::streamsize>(FILE_SIZE));
        auto n = static_cast<size_t>(ifs.gcount());
        result.bytes_read += n;
        result.files_scanned++;
        for (size_t j = 0; j < n; ++j) {
            if (static_cast<std::byte>(buf[j]) == NEEDLE) {
                result.matches++;
                break;
            }
        }
    }
    return result;
}

/// LABIOS: simulate pipeline-based scan.
/// Create one label per file with a filter_bytes pipeline stage,
/// execute all pipelines, and count matches.
ScanResult labios_pipeline_scan(const fs::path& dir,
                                labios::sds::ProgramRepository& repo) {
    ScanResult result;

    // Build a single-stage filter pipeline
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back(
        {"builtin://filter_bytes", std::to_string(static_cast<int>(NEEDLE)), -1, -1});

    for (int i = 0; i < FILE_COUNT; ++i) {
        auto path = dir / ("file_" + std::to_string(i) + ".dat");

        // Read file (worker would do this at storage)
        std::ifstream ifs(path, std::ios::binary);
        std::vector<std::byte> data(FILE_SIZE);
        ifs.read(reinterpret_cast<char*>(data.data()),
                 static_cast<std::streamsize>(FILE_SIZE));
        auto n = static_cast<size_t>(ifs.gcount());
        result.bytes_read += n;
        result.files_scanned++;

        // Execute pipeline at worker (filter_bytes keeps only matching bytes)
        auto stage_result = labios::sds::execute_pipeline(pipeline, data, repo);
        if (stage_result.success && !stage_result.data.empty()) {
            result.matches++;
        }
    }
    return result;
}

using hrc = std::chrono::high_resolution_clock;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: both paths find the same matches
// ---------------------------------------------------------------------------

TEST_CASE("Sequential scan: vanilla and LABIOS find same matches",
          "[bench][sequential_scan]") {
    auto dir = fs::temp_directory_path() / "labios_bench_pscan";
    create_test_files(dir);

    auto vanilla = vanilla_sequential_scan(dir);
    REQUIRE(vanilla.files_scanned == FILE_COUNT);
    REQUIRE(vanilla.bytes_read == FILE_COUNT * FILE_SIZE);
    // Every 10th file has needle: 0, 10, 20, ... 490 = 50 files
    REQUIRE(vanilla.matches == 50);

    labios::sds::ProgramRepository repo;
    auto labios_result = labios_pipeline_scan(dir, repo);
    REQUIRE(labios_result.files_scanned == FILE_COUNT);
    REQUIRE(labios_result.matches == vanilla.matches);

    cleanup_test_files(dir);
}

TEST_CASE("Sequential scan: label carries pipeline metadata",
          "[bench][sequential_scan]") {
    labios::LabelData label;
    label.id = labios::generate_label_id(1);
    label.type = labios::LabelType::Read;
    label.source_uri = "file:///workspace/src/**/*.cpp";
    label.operation = "scan";
    label.intent = labios::Intent::ToolOutput;
    label.pipeline.stages.push_back(
        {"builtin://filter_bytes", "66", -1, -1});

    auto buf = labios::serialize_label(label);
    auto rt = labios::deserialize_label(buf);
    REQUIRE(rt.pipeline.stages.size() == 1);
    REQUIRE(rt.pipeline.stages[0].operation == "builtin://filter_bytes");
    REQUIRE(rt.source_uri == "file:///workspace/src/**/*.cpp");
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

// True parallel dispatch requires the live worker pool (NATS + dispatcher).
// This benchmark runs sequentially to measure pipeline overhead in isolation.
TEST_CASE("Sequential scan benchmarks", "[bench][sequential_scan][!benchmark]") {
    auto dir = fs::temp_directory_path() / "labios_bench_pscan_perf";
    create_test_files(dir);
    labios::sds::ProgramRepository repo;

    BENCHMARK("Vanilla: sequential scan 500 x 1KB files") {
        return vanilla_sequential_scan(dir);
    };

    BENCHMARK("LABIOS: pipeline scan 500 x 1KB files") {
        return labios_pipeline_scan(dir, repo);
    };

    // Measure label creation overhead for batch dispatch
    BENCHMARK("Label creation overhead: 500 read labels") {
        std::vector<labios::LabelData> labels;
        labels.reserve(FILE_COUNT);
        for (int i = 0; i < FILE_COUNT; ++i) {
            labios::LabelData label;
            label.id = labios::generate_label_id(1);
            label.type = labios::LabelType::Read;
            label.source_uri = "file:///workspace/file_" + std::to_string(i) + ".dat";
            label.pipeline.stages.push_back(
                {"builtin://filter_bytes", "66", -1, -1});
            labels.push_back(std::move(label));
        }
        return labels.size();
    };

    // Measure just the pipeline execution (no I/O)
    std::vector<std::byte> sample_data(FILE_SIZE);
    std::fill(sample_data.begin(), sample_data.begin() + 16, NEEDLE);
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back(
        {"builtin://filter_bytes", std::to_string(static_cast<int>(NEEDLE)), -1, -1});

    BENCHMARK("Pipeline execution only: filter_bytes 1KB x 500") {
        int matches = 0;
        for (int i = 0; i < FILE_COUNT; ++i) {
            auto r = labios::sds::execute_pipeline(pipeline, sample_data, repo);
            if (r.success && !r.data.empty()) ++matches;
        }
        return matches;
    };

    cleanup_test_files(dir);
}
