#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

struct BenchResult {
    double write_mbps;
    double read_mbps;
    double write_sec;
    double read_sec;
    bool verify_ok;
};

static BenchResult run_benchmark(labios::Client& client,
                                  uint64_t chunk_size, int num_chunks,
                                  const std::string& prefix) {
    uint64_t total = chunk_size * num_chunks;

    std::vector<std::byte> data(chunk_size);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + chunk_size,
              static_cast<uint8_t>(0));

    // Write
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < num_chunks; ++i) {
        std::string path = prefix + std::to_string(i) + ".bin";
        client.write(path, data);
    }
    auto t1 = std::chrono::steady_clock::now();
    double write_sec = std::chrono::duration<double>(t1 - t0).count();

    // Read + verify
    auto t2 = std::chrono::steady_clock::now();
    bool verify_ok = true;
    for (int i = 0; i < num_chunks; ++i) {
        std::string path = prefix + std::to_string(i) + ".bin";
        auto result = client.read(path, 0, chunk_size);
        if (result.size() != chunk_size ||
            !std::equal(result.begin(), result.end(), data.begin())) {
            verify_ok = false;
        }
    }
    auto t3 = std::chrono::steady_clock::now();
    double read_sec = std::chrono::duration<double>(t3 - t2).count();

    double mb = static_cast<double>(total) / (1024.0 * 1024.0);
    return {mb / write_sec, mb / read_sec, write_sec, read_sec, verify_ok};
}

TEST_CASE("Benchmark: 100MB sequential 1MB chunks", "[benchmark]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    auto r = run_benchmark(client, 1024 * 1024, 100, "/bench/seq1m_");

    std::cout << "\n=== 100MB Sequential (1MB chunks) ===\n"
              << "  Write: " << r.write_mbps << " MB/s (" << r.write_sec << "s)\n"
              << "  Read:  " << r.read_mbps << " MB/s (" << r.read_sec << "s)\n"
              << "  Data:  " << (r.verify_ok ? "VERIFIED" : "CORRUPT") << "\n";

    REQUIRE(r.verify_ok);
    // Regression thresholds: must sustain at least 15 MB/s write, 15 MB/s read
    // in Docker Compose on a dev machine. DragonflyDB uses io_uring internally,
    // which has limited support in WSL2 containers, so thresholds are
    // conservative to avoid false failures in development environments.
    CHECK(r.write_mbps > 15.0);
    CHECK(r.read_mbps > 15.0);
}

TEST_CASE("Benchmark: 10MB split write (10x1MB labels)", "[benchmark]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // Single 10MB file, the LabelManager splits into 10 labels
    uint64_t total = 10 * 1024 * 1024;
    std::vector<std::byte> data(total);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + total,
              static_cast<uint8_t>(0));

    auto t0 = std::chrono::steady_clock::now();
    client.write("/bench/split10m.bin", data);
    auto t1 = std::chrono::steady_clock::now();
    double write_sec = std::chrono::duration<double>(t1 - t0).count();

    auto t2 = std::chrono::steady_clock::now();
    auto result = client.read("/bench/split10m.bin", 0, total);
    auto t3 = std::chrono::steady_clock::now();
    double read_sec = std::chrono::duration<double>(t3 - t2).count();

    double mb = static_cast<double>(total) / (1024.0 * 1024.0);

    std::cout << "\n=== 10MB Split Write (10 labels) ===\n"
              << "  Write: " << (mb / write_sec) << " MB/s (" << write_sec << "s)\n"
              << "  Read:  " << (mb / read_sec) << " MB/s (" << read_sec << "s)\n"
              << "  Data:  " << (result.size() == total ? "VERIFIED" : "CORRUPT") << "\n";

    REQUIRE(result.size() == total);
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST_CASE("Benchmark: 1000 small files (1KB each)", "[benchmark]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    auto r = run_benchmark(client, 1024, 1000, "/bench/small_");

    std::cout << "\n=== 1000 Small Files (1KB each) ===\n"
              << "  Write: " << r.write_mbps << " MB/s (" << r.write_sec << "s)\n"
              << "  Read:  " << r.read_mbps << " MB/s (" << r.read_sec << "s)\n"
              << "  IOPS:  write=" << (1000 / r.write_sec)
              << " read=" << (1000 / r.read_sec) << "\n"
              << "  Data:  " << (r.verify_ok ? "VERIFIED" : "CORRUPT") << "\n";

    REQUIRE(r.verify_ok);
}
