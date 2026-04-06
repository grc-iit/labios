#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/workspace.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr int KEY_COUNT = 50;
constexpr size_t VALUE_SIZE = 2048; // 2KB per key (simulating source file knowledge)

bool redis_available() {
    try {
        const char* host = std::getenv("LABIOS_REDIS_HOST");
        labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
        return redis.connected();
    } catch (...) {
        return false;
    }
}

std::vector<std::byte> make_knowledge(int key_index) {
    std::vector<std::byte> data(VALUE_SIZE);
    // Simulate structured knowledge: key index in first bytes, rest filled
    auto tag = static_cast<uint8_t>(key_index & 0xFF);
    std::fill(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + VALUE_SIZE,
              tag);
    // Put key index at offset 0 for verification
    std::memcpy(data.data(), &key_index, sizeof(key_index));
    return data;
}

/// Create test files on disk (simulating source files an agent would re-read).
void create_source_files(const fs::path& dir) {
    fs::create_directories(dir);
    for (int i = 0; i < KEY_COUNT; ++i) {
        auto path = dir / ("source_" + std::to_string(i) + ".cpp");
        auto data = make_knowledge(i);
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
}

struct RecallResult {
    int keys_retrieved = 0;
    uint64_t bytes_read = 0;
    bool all_valid = true;
};

/// Vanilla: re-read 50 source files from disk to rebuild understanding.
RecallResult vanilla_file_recall(const fs::path& dir) {
    RecallResult result;
    for (int i = 0; i < KEY_COUNT; ++i) {
        auto path = dir / ("source_" + std::to_string(i) + ".cpp");
        std::ifstream ifs(path, std::ios::binary);
        std::vector<char> buf(VALUE_SIZE);
        ifs.read(buf.data(), static_cast<std::streamsize>(VALUE_SIZE));
        auto n = static_cast<size_t>(ifs.gcount());
        result.bytes_read += n;
        result.keys_retrieved++;

        // Verify content
        int stored_index = 0;
        std::memcpy(&stored_index, buf.data(), sizeof(stored_index));
        if (stored_index != i) result.all_valid = false;
    }
    return result;
}

/// LABIOS: recall 50 keys from workspace (single Redis GET per key).
RecallResult labios_workspace_recall(labios::Workspace& ws, uint32_t agent_id) {
    RecallResult result;
    for (int i = 0; i < KEY_COUNT; ++i) {
        auto key = "knowledge_" + std::to_string(i);
        auto data = ws.get(key, agent_id);
        if (data.has_value()) {
            result.keys_retrieved++;
            result.bytes_read += data->size();

            // Verify content
            int stored_index = 0;
            if (data->size() >= sizeof(stored_index)) {
                std::memcpy(&stored_index, data->data(), sizeof(stored_index));
                if (stored_index != i) result.all_valid = false;
            }
        } else {
            result.all_valid = false;
        }
    }
    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: both paths retrieve identical knowledge
// ---------------------------------------------------------------------------

TEST_CASE("Session recall: vanilla re-reads all source files",
          "[bench][session_recall]") {
    auto dir = fs::temp_directory_path() / "labios_bench_recall";
    create_source_files(dir);

    auto result = vanilla_file_recall(dir);
    REQUIRE(result.keys_retrieved == KEY_COUNT);
    REQUIRE(result.bytes_read == KEY_COUNT * VALUE_SIZE);
    REQUIRE(result.all_valid);

    fs::remove_all(dir);
}

TEST_CASE("Session recall: workspace retrieves pre-stored knowledge",
          "[bench][session_recall]") {
    if (!redis_available()) SKIP("Redis not available");

    const char* host = std::getenv("LABIOS_REDIS_HOST");
    labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
    labios::WorkspaceRegistry registry(redis);

    constexpr uint32_t agent_id = 1;
    auto* ws = registry.create("bench_recall_test", agent_id);
    REQUIRE(ws != nullptr);

    // Pre-populate workspace (simulating prior session storage)
    for (int i = 0; i < KEY_COUNT; ++i) {
        auto key = "knowledge_" + std::to_string(i);
        auto data = make_knowledge(i);
        ws->put(key, data, agent_id);
    }

    // Recall
    auto result = labios_workspace_recall(*ws, agent_id);
    REQUIRE(result.keys_retrieved == KEY_COUNT);
    REQUIRE(result.bytes_read == KEY_COUNT * VALUE_SIZE);
    REQUIRE(result.all_valid);

    ws->destroy();
    registry.remove("bench_recall_test");
}

TEST_CASE("Session recall: workspace versioning preserves history",
          "[bench][session_recall]") {
    if (!redis_available()) SKIP("Redis not available");

    const char* host = std::getenv("LABIOS_REDIS_HOST");
    labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
    labios::WorkspaceRegistry registry(redis);

    auto* ws = registry.create("bench_recall_version", 1);
    REQUIRE(ws != nullptr);

    auto v1_data = make_knowledge(100);
    auto v1 = ws->put("evolving_key", v1_data, 1);

    auto v2_data = make_knowledge(200);
    auto v2 = ws->put("evolving_key", v2_data, 1);

    REQUIRE(v2 > v1);

    // Latest read returns v2
    auto latest = ws->get("evolving_key", 1);
    REQUIRE(latest.has_value());
    int idx = 0;
    std::memcpy(&idx, latest->data(), sizeof(idx));
    REQUIRE(idx == 200);

    // Version-specific read returns v1
    auto historical = ws->get_version("evolving_key", v1, 1);
    REQUIRE(historical.has_value());
    std::memcpy(&idx, historical->data(), sizeof(idx));
    REQUIRE(idx == 100);

    ws->destroy();
    registry.remove("bench_recall_version");
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Session recall benchmarks", "[bench][session_recall][!benchmark]") {
    if (!redis_available()) SKIP("Redis not available");

    auto dir = fs::temp_directory_path() / "labios_bench_recall_perf";
    create_source_files(dir);

    BENCHMARK("Vanilla: re-read 50 x 2KB source files") {
        return vanilla_file_recall(dir);
    };

    {
        const char* host = std::getenv("LABIOS_REDIS_HOST");
        labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
        labios::WorkspaceRegistry registry(redis);

        auto* ws = registry.create("bench_recall_perf", 1);
        for (int i = 0; i < KEY_COUNT; ++i) {
            ws->put("knowledge_" + std::to_string(i), make_knowledge(i), 1);
        }

        BENCHMARK("LABIOS: workspace recall 50 x 2KB keys") {
            return labios_workspace_recall(*ws, 1);
        };

        // Measure individual workspace GET latency
        auto single_value = make_knowledge(0);
        ws->put("single_bench_key", single_value, 1);

        BENCHMARK("LABIOS: single workspace GET 2KB") {
            return ws->get("single_bench_key", 1);
        };

        ws->destroy();
        registry.remove("bench_recall_perf");
    }

    // Baseline: single file read
    BENCHMARK("Baseline: single 2KB file read") {
        auto path = dir / "source_0.cpp";
        std::ifstream ifs(path, std::ios::binary);
        std::vector<char> buf(VALUE_SIZE);
        ifs.read(buf.data(), static_cast<std::streamsize>(VALUE_SIZE));
        return buf.size();
    };

    fs::remove_all(dir);
}
