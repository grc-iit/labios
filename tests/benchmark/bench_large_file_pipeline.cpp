#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/label.h>
#include <labios/sds/executor.h>
#include <labios/sds/program_repo.h>
#include <labios/sds/types.h>

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

constexpr size_t FILE_10MB = 10 * 1024 * 1024;
constexpr std::byte MATCH_BYTE{0xEE};  // "matching" log lines contain this
constexpr int MATCH_LINE_INTERVAL = 100; // every 100th "line" matches
constexpr size_t LINE_SIZE = 128;        // simulated line width

/// Generate a 10MB buffer simulating a log file.
/// Every MATCH_LINE_INTERVAL-th line starts with MATCH_BYTE.
std::vector<std::byte> generate_log_data() {
    size_t line_count = FILE_10MB / LINE_SIZE;
    std::vector<std::byte> data(line_count * LINE_SIZE);

    for (size_t i = 0; i < line_count; ++i) {
        auto offset = i * LINE_SIZE;
        // Fill line with non-matching content
        auto fill = static_cast<std::byte>((i * 7 + 3) & 0xFF);
        // Avoid accidentally using MATCH_BYTE as fill
        if (fill == MATCH_BYTE) fill = std::byte{0x01};
        std::fill(data.data() + offset, data.data() + offset + LINE_SIZE, fill);

        // Mark matching lines
        if (i % MATCH_LINE_INTERVAL == 0) {
            std::fill(data.data() + offset,
                      data.data() + offset + LINE_SIZE,
                      MATCH_BYTE);
        }
    }
    return data;
}

/// Write the log data to a file on disk.
void write_log_file(const fs::path& path, const std::vector<std::byte>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

struct PipelineResult {
    int matching_lines = 0;
    uint64_t bytes_read = 0;       // Total bytes read from source
    uint64_t bytes_returned = 0;   // Bytes returned to client
};

/// Vanilla: read entire 10MB into memory, scan for matching lines, collect.
PipelineResult vanilla_filter(const fs::path& path) {
    PipelineResult result;

    std::ifstream ifs(path, std::ios::binary);
    std::vector<std::byte> data(FILE_10MB);
    ifs.read(reinterpret_cast<char*>(data.data()),
             static_cast<std::streamsize>(FILE_10MB));
    result.bytes_read = static_cast<uint64_t>(ifs.gcount());

    // Filter: scan for lines starting with MATCH_BYTE
    size_t line_count = result.bytes_read / LINE_SIZE;
    std::vector<std::byte> output;
    for (size_t i = 0; i < line_count; ++i) {
        if (data[i * LINE_SIZE] == MATCH_BYTE) {
            output.insert(output.end(),
                          data.data() + i * LINE_SIZE,
                          data.data() + (i + 1) * LINE_SIZE);
            result.matching_lines++;
        }
    }
    result.bytes_returned = output.size();
    return result;
}

/// Vanilla from memory buffer (avoids disk I/O for fairer pipeline comparison).
PipelineResult vanilla_filter_memory(const std::vector<std::byte>& data) {
    PipelineResult result;
    result.bytes_read = data.size();

    size_t line_count = data.size() / LINE_SIZE;
    std::vector<std::byte> output;
    for (size_t i = 0; i < line_count; ++i) {
        if (data[i * LINE_SIZE] == MATCH_BYTE) {
            output.insert(output.end(),
                          data.data() + i * LINE_SIZE,
                          data.data() + (i + 1) * LINE_SIZE);
            result.matching_lines++;
        }
    }
    result.bytes_returned = output.size();
    return result;
}

/// LABIOS: pipeline filter + truncate executes at storage tier.
/// Only matching bytes are returned (filter_bytes keeps MATCH_BYTE bytes only),
/// then truncate limits output size.
PipelineResult labios_pipeline_filter(const std::vector<std::byte>& data,
                                       labios::sds::ProgramRepository& repo) {
    PipelineResult result;
    result.bytes_read = data.size();

    // Pipeline: filter_bytes(MATCH_BYTE) -> truncate(limit)
    // filter_bytes keeps only bytes equal to MATCH_BYTE
    // truncate caps output at a maximum size
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back(
        {"builtin://filter_bytes",
         std::to_string(static_cast<int>(MATCH_BYTE)), -1, -1});

    // Truncate to ~100 matching lines worth of filtered bytes
    // Each matching line has LINE_SIZE bytes of MATCH_BYTE
    constexpr size_t MAX_RESULT = 100 * LINE_SIZE;
    pipeline.stages.push_back(
        {"builtin://truncate", std::to_string(MAX_RESULT), -1, -1});

    auto stage_result = labios::sds::execute_pipeline(pipeline, data, repo);
    if (stage_result.success) {
        result.bytes_returned = stage_result.data.size();
        // Each MATCH_BYTE in output represents one byte from a matching line
        // Count matching lines as groups of LINE_SIZE matching bytes
        result.matching_lines = static_cast<int>(
            stage_result.data.size() / LINE_SIZE);
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: both paths find matching lines
// ---------------------------------------------------------------------------

TEST_CASE("Large file pipeline: vanilla finds correct match count",
          "[bench][large_pipeline]") {
    auto log_data = generate_log_data();
    auto result = vanilla_filter_memory(log_data);

    // Line 0, 100, 200, ... floor((total_lines-1)/interval)+1
    size_t total_lines = FILE_10MB / LINE_SIZE;
    size_t expected = (total_lines + MATCH_LINE_INTERVAL - 1) / MATCH_LINE_INTERVAL;

    REQUIRE(result.matching_lines == static_cast<int>(expected));
    REQUIRE(result.bytes_read == log_data.size());
    REQUIRE(result.bytes_returned == expected * LINE_SIZE);
}

TEST_CASE("Large file pipeline: LABIOS filter+truncate produces bounded output",
          "[bench][large_pipeline]") {
    auto log_data = generate_log_data();
    labios::sds::ProgramRepository repo;

    auto result = labios_pipeline_filter(log_data, repo);
    REQUIRE(result.bytes_read == log_data.size());
    // Truncate caps at 100 * LINE_SIZE = 12800 bytes
    constexpr size_t MAX_RESULT = 100 * LINE_SIZE;
    REQUIRE(result.bytes_returned <= MAX_RESULT);
    // Must have found some matches
    REQUIRE(result.matching_lines > 0);
    REQUIRE(result.matching_lines <= 100);
}

TEST_CASE("Large file pipeline: label carries pipeline and URI metadata",
          "[bench][large_pipeline]") {
    labios::LabelData label;
    label.id = labios::generate_label_id(1);
    label.type = labios::LabelType::Read;
    label.source_uri = "file:///var/log/agent_session.log";
    label.operation = "pipeline_filter";
    label.intent = labios::Intent::ToolOutput;
    label.data_size = FILE_10MB;
    label.pipeline.stages.push_back(
        {"builtin://filter_bytes", "238", -1, -1}); // 0xEE = 238
    label.pipeline.stages.push_back(
        {"builtin://truncate", "12800", -1, -1});

    auto buf = labios::serialize_label(label);
    auto rt = labios::deserialize_label(buf);
    REQUIRE(rt.pipeline.stages.size() == 2);
    REQUIRE(rt.pipeline.stages[0].operation == "builtin://filter_bytes");
    REQUIRE(rt.pipeline.stages[1].operation == "builtin://truncate");
    REQUIRE(rt.data_size == FILE_10MB);
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Large file pipeline benchmarks",
          "[bench][large_pipeline][!benchmark]") {
    // Generate data once, reuse across benchmarks
    auto log_data = generate_log_data();
    labios::sds::ProgramRepository repo;

    // Disk-based vanilla (includes I/O)
    auto dir = fs::temp_directory_path() / "labios_bench_largepipe";
    auto log_path = dir / "session.log";
    write_log_file(log_path, log_data);

    BENCHMARK("Vanilla: read 10MB + filter in memory (disk)") {
        return vanilla_filter(log_path);
    };

    BENCHMARK("Vanilla: filter 10MB in memory (no disk)") {
        return vanilla_filter_memory(log_data);
    };

    BENCHMARK("LABIOS: filter+truncate pipeline 10MB") {
        return labios_pipeline_filter(log_data, repo);
    };

    // Measure pipeline setup overhead
    BENCHMARK("Pipeline setup: serialize 2-stage pipeline label") {
        labios::LabelData label;
        label.id = labios::generate_label_id(1);
        label.type = labios::LabelType::Read;
        label.source_uri = "file:///var/log/session.log";
        label.data_size = FILE_10MB;
        label.pipeline.stages.push_back(
            {"builtin://filter_bytes", "238", -1, -1});
        label.pipeline.stages.push_back(
            {"builtin://truncate", "12800", -1, -1});
        return labios::serialize_label(label);
    };

    // Measure filter_bytes alone at scale
    labios::sds::Pipeline filter_only;
    filter_only.stages.push_back(
        {"builtin://filter_bytes",
         std::to_string(static_cast<int>(MATCH_BYTE)), -1, -1});

    BENCHMARK("Pipeline stage: filter_bytes 10MB") {
        return labios::sds::execute_pipeline(filter_only, log_data, repo);
    };

    // Measure truncate alone
    labios::sds::Pipeline truncate_only;
    truncate_only.stages.push_back(
        {"builtin://truncate", "12800", -1, -1});

    BENCHMARK("Pipeline stage: truncate 10MB to 12.5KB") {
        return labios::sds::execute_pipeline(truncate_only, log_data, repo);
    };

    fs::remove_all(dir);
}
