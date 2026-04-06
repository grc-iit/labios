#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/content_manager.h>
#include <labios/transport/redis.h>

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

constexpr int WRITE_COUNT = 1000;
constexpr size_t WRITE_SIZE = 100; // 100 bytes each

bool redis_available() {
    try {
        const char* host = std::getenv("LABIOS_REDIS_HOST");
        labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
        return redis.connected();
    } catch (...) {
        return false;
    }
}

std::vector<std::byte> make_write_payload(int sequence) {
    std::vector<std::byte> data(WRITE_SIZE);
    // Fill with sequence-derived pattern so writes are distinguishable
    auto val = static_cast<uint8_t>(sequence & 0xFF);
    std::fill(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + WRITE_SIZE,
              val);
    return data;
}

struct SmallIoResult {
    int writes_issued = 0;
    int flush_ops = 0;
    uint64_t bytes_written = 0;
};

/// Vanilla: 1000 separate write() calls, each opening/appending/closing.
SmallIoResult vanilla_small_writes(const fs::path& filepath) {
    SmallIoResult result;
    // Truncate first
    { std::ofstream ofs(filepath, std::ios::binary | std::ios::trunc); }

    for (int i = 0; i < WRITE_COUNT; ++i) {
        auto payload = make_write_payload(i);
        std::ofstream ofs(filepath, std::ios::binary | std::ios::app);
        ofs.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        result.writes_issued++;
        result.bytes_written += payload.size();
    }
    result.flush_ops = WRITE_COUNT; // Each write is a separate I/O
    return result;
}

/// LABIOS: 1000 writes through content manager cache, then flush.
/// Cache aggregates small writes into batched flushes. The key metric
/// is flush_ops: how many flush events were triggered (each batching
/// multiple writes into one backend call).
SmallIoResult labios_cached_writes(labios::ContentManager& cm,
                                    const std::string& filepath) {
    SmallIoResult result;
    int fd = 42; // Simulated file descriptor

    for (int i = 0; i < WRITE_COUNT; ++i) {
        auto payload = make_write_payload(i);
        auto offset = static_cast<uint64_t>(i) * WRITE_SIZE;
        auto regions = cm.cache_write(fd, filepath, offset, payload);
        result.writes_issued++;
        result.bytes_written += payload.size();
        // Count flush events, not individual regions.
        // Each non-empty return represents one batched flush to backend.
        if (!regions.empty()) {
            result.flush_ops++;
        }
    }

    // Final explicit flush for remaining cached data
    auto final_regions = cm.flush(fd);
    if (!final_regions.empty()) {
        result.flush_ops++;
    }

    return result;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: cache aggregates writes, reducing flush operations
// ---------------------------------------------------------------------------

TEST_CASE("Small I/O: vanilla issues one op per write",
          "[bench][small_io]") {
    auto dir = fs::temp_directory_path() / "labios_bench_smallio";
    fs::create_directories(dir);
    auto filepath = dir / "output.dat";

    auto result = vanilla_small_writes(filepath);
    REQUIRE(result.writes_issued == WRITE_COUNT);
    REQUIRE(result.flush_ops == WRITE_COUNT);
    REQUIRE(result.bytes_written == WRITE_COUNT * WRITE_SIZE);

    // Verify file size
    auto size = fs::file_size(filepath);
    REQUIRE(size == WRITE_COUNT * WRITE_SIZE);

    fs::remove_all(dir);
}

TEST_CASE("Small I/O: content manager aggregates writes",
          "[bench][small_io]") {
    if (!redis_available()) SKIP("Redis not available");

    const char* host = std::getenv("LABIOS_REDIS_HOST");
    labios::transport::RedisConnection redis(host ? host : "localhost", 6379);

    // min_label_size of 8KB means writes accumulate before flushing
    labios::ContentManager cm(redis,
                               /*min_label_size=*/8192,
                               /*flush_interval_ms=*/0,
                               labios::ReadPolicy::WriteOnly);

    auto result = labios_cached_writes(cm, "/tmp/labios_bench_smallio_out");

    REQUIRE(result.writes_issued == WRITE_COUNT);
    REQUIRE(result.bytes_written == WRITE_COUNT * WRITE_SIZE);
    // Cache batches writes: 1000 x 100B = 100KB, with 8KB threshold
    // produces ~12 flush events + 1 final = ~13, far fewer than 1000
    CHECK(result.flush_ops < 20);
    INFO("Flush events: " << result.flush_ops << " vs writes: " << WRITE_COUNT);
}

TEST_CASE("Small I/O: cache preserves write ordering",
          "[bench][small_io]") {
    if (!redis_available()) SKIP("Redis not available");

    const char* host = std::getenv("LABIOS_REDIS_HOST");
    labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);

    int fd = 99;
    std::string path = "/tmp/labios_bench_order_test";

    // Write 10 sequential chunks
    for (int i = 0; i < 10; ++i) {
        auto payload = make_write_payload(i);
        cm.cache_write(fd, path, static_cast<uint64_t>(i) * WRITE_SIZE, payload);
    }

    // Read back from cache at specific offsets
    auto read_0 = cm.cache_read(fd, 0, WRITE_SIZE);
    REQUIRE(read_0.has_value());
    REQUIRE(read_0->size() == WRITE_SIZE);
    // First write had pattern byte 0
    REQUIRE((*read_0)[0] == std::byte{0});

    auto read_5 = cm.cache_read(fd, 5 * WRITE_SIZE, WRITE_SIZE);
    REQUIRE(read_5.has_value());
    REQUIRE((*read_5)[0] == std::byte{5});

    cm.evict(fd);
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Small I/O benchmarks", "[bench][small_io][!benchmark]") {
    if (!redis_available()) SKIP("Redis not available");

    auto dir = fs::temp_directory_path() / "labios_bench_smallio_perf";
    fs::create_directories(dir);
    auto filepath = dir / "output.dat";

    BENCHMARK("Vanilla: 1000 x 100B separate writes") {
        return vanilla_small_writes(filepath);
    };

    {
        const char* host = std::getenv("LABIOS_REDIS_HOST");
        labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
        labios::ContentManager cm(redis, 8192, 0, labios::ReadPolicy::WriteOnly);

        BENCHMARK("LABIOS: 1000 x 100B cached writes (8KB threshold)") {
            return labios_cached_writes(cm, "/tmp/labios_bench_smallio_perf");
        };

        labios::ContentManager cm_4k(redis, 4096, 0, labios::ReadPolicy::WriteOnly);
        BENCHMARK("LABIOS: 1000 x 100B cached writes (4KB threshold)") {
            return labios_cached_writes(cm_4k, "/tmp/labios_bench_smallio_perf_4k");
        };
    }

    // Baseline: raw memory copy (no I/O overhead)
    BENCHMARK("Baseline: 1000 x 100B memcpy") {
        std::vector<std::byte> dest(WRITE_COUNT * WRITE_SIZE);
        for (int i = 0; i < WRITE_COUNT; ++i) {
            auto payload = make_write_payload(i);
            std::memcpy(dest.data() + i * WRITE_SIZE,
                        payload.data(), WRITE_SIZE);
        }
        return dest.size();
    };

    fs::remove_all(dir);
}
