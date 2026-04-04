// tests/integration/kernel_cm1_test.cpp
//
// CM1 Application Kernel: Sequential Write-Intensive Bursts
//
// Paper reference: HPDC'19 Section 2.2(a), Section 4 (16x speedup)
// Pattern: periodic sequential writes (atmospheric simulation output)
// Goal: demonstrate async I/O throughput advantage over sync

#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

// Simulate a "compute phase" between I/O bursts.
// In real CM1, this is the atmospheric model stepping forward.
static void simulate_compute(int timestep_ms) {
    auto start = std::chrono::steady_clock::now();
    // Burn CPU (not sleep) to simulate actual work.
    volatile double x = 1.0;
    while (true) {
        for (int i = 0; i < 1000; ++i) x *= 1.000001;
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= std::chrono::milliseconds(timestep_ms)) break;
    }
}

TEST_CASE("CM1 kernel: sync write burst", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024; // 1MB per timestep (scaled down for Docker)
    constexpr int compute_ms = 50; // 50ms compute per timestep

    std::vector<std::byte> grid(output_size);
    std::iota(reinterpret_cast<uint8_t*>(grid.data()),
              reinterpret_cast<uint8_t*>(grid.data()) + output_size,
              static_cast<uint8_t>(0));

    auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < timesteps; ++t) {
        // Compute phase.
        simulate_compute(compute_ms);

        // I/O phase: sync, blocks until done.
        std::string path = "/cm1/sync/step_" + std::to_string(t) + ".dat";
        grid[0] = static_cast<std::byte>(t); // Mark timestep in data.
        client.write(path, grid);
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    std::cout << "\n=== CM1 Sync Mode ===\n"
              << "  Timesteps: " << timesteps << "\n"
              << "  Total time: " << total_sec << "s\n"
              << "  I/O throughput: " << (data_mb / total_sec) << " MB/s\n"
              << "  Effective rate: " << (timesteps / total_sec) << " steps/s\n";

    // Verify last timestep data.
    auto result = client.read("/cm1/sync/step_9.dat", 0, output_size);
    REQUIRE(result.size() == output_size);
    CHECK(result[0] == static_cast<std::byte>(9));
}

TEST_CASE("CM1 kernel: async write burst", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;

    std::vector<std::byte> grid(output_size);
    std::iota(reinterpret_cast<uint8_t*>(grid.data()),
              reinterpret_cast<uint8_t*>(grid.data()) + output_size,
              static_cast<uint8_t>(0));

    std::vector<labios::PendingIO> pending;

    auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < timesteps; ++t) {
        // Compute phase runs while previous I/O completes in background.
        simulate_compute(compute_ms);

        // I/O phase: async, returns immediately.
        std::string path = "/cm1/async/step_" + std::to_string(t) + ".dat";
        grid[0] = static_cast<std::byte>(t);
        pending.push_back(client.async_write(path, grid));
    }

    // Barrier: wait for all outstanding I/O.
    for (auto& p : pending) {
        client.wait(p);
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    std::cout << "\n=== CM1 Async Mode ===\n"
              << "  Timesteps: " << timesteps << "\n"
              << "  Total time: " << total_sec << "s\n"
              << "  I/O throughput: " << (data_mb / total_sec) << " MB/s\n"
              << "  Effective rate: " << (timesteps / total_sec) << " steps/s\n";

    // Verify last timestep data.
    auto result = client.read("/cm1/async/step_9.dat", 0, output_size);
    REQUIRE(result.size() == output_size);
    CHECK(result[0] == static_cast<std::byte>(9));
}

TEST_CASE("CM1 kernel: async pipelined with periodic barriers", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;
    constexpr int barrier_interval = 3; // Wait every 3 timesteps.

    std::vector<std::byte> grid(output_size);
    std::iota(reinterpret_cast<uint8_t*>(grid.data()),
              reinterpret_cast<uint8_t*>(grid.data()) + output_size,
              static_cast<uint8_t>(0));

    std::vector<labios::PendingIO> batch;

    auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < timesteps; ++t) {
        simulate_compute(compute_ms);

        std::string path = "/cm1/pipelined/step_" + std::to_string(t) + ".dat";
        grid[0] = static_cast<std::byte>(t);
        batch.push_back(client.async_write(path, grid));

        // Periodic barrier to bound outstanding I/O.
        if ((t + 1) % barrier_interval == 0) {
            for (auto& p : batch) client.wait(p);
            batch.clear();
        }
    }
    // Drain remaining.
    for (auto& p : batch) client.wait(p);

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    std::cout << "\n=== CM1 Pipelined Mode (barrier every " << barrier_interval << ") ===\n"
              << "  Timesteps: " << timesteps << "\n"
              << "  Total time: " << total_sec << "s\n"
              << "  I/O throughput: " << (data_mb / total_sec) << " MB/s\n"
              << "  Effective rate: " << (timesteps / total_sec) << " steps/s\n";

    // Verify data integrity across all timesteps.
    for (int t = 0; t < timesteps; ++t) {
        std::string path = "/cm1/pipelined/step_" + std::to_string(t) + ".dat";
        auto result = client.read(path, 0, output_size);
        REQUIRE(result.size() == output_size);
        CHECK(result[0] == static_cast<std::byte>(t));
    }
}
