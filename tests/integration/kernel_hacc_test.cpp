// tests/integration/kernel_hacc_test.cpp
//
// HACC Application Kernel: Checkpoint Overwrite Pattern
//
// Paper reference: HPDC'19 Section 2.2(b)
// Pattern: periodic checkpoint writes, file-per-process, overwrite at same offset
// Goal: verify WAW dependency resolution and correct final state after overwrites

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
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

TEST_CASE("HACC kernel: single process checkpoint overwrite", "[kernel][hacc]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 20;
    constexpr size_t ckpt_size = 256 * 1024; // 256KB checkpoint

    std::vector<std::byte> state(ckpt_size);

    auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < timesteps; ++t) {
        // Fill state with timestep-specific pattern.
        // Each byte = (t + byte_index) & 0xFF, so every timestep is unique.
        for (size_t i = 0; i < ckpt_size; ++i) {
            state[i] = static_cast<std::byte>((t + i) & 0xFF);
        }

        // Overwrite the same file at offset 0 every timestep.
        // This creates WAW dependencies that the shuffler must resolve.
        client.write("/hacc/rank0_checkpoint.dat", state, 0);
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();

    // Read back the checkpoint: must be the LAST timestep's data.
    auto result = client.read("/hacc/rank0_checkpoint.dat", 0, ckpt_size);
    REQUIRE(result.size() == ckpt_size);

    int final_t = timesteps - 1;
    bool correct = true;
    for (size_t i = 0; i < ckpt_size; ++i) {
        if (result[i] != static_cast<std::byte>((final_t + i) & 0xFF)) {
            correct = false;
            break;
        }
    }

    std::cout << "\n=== HACC Single-Process Checkpoint ===\n"
              << "  Timesteps: " << timesteps << "\n"
              << "  Checkpoint size: " << (ckpt_size / 1024) << " KB\n"
              << "  Total time: " << total_sec << "s\n"
              << "  Checkpoint rate: " << (timesteps / total_sec) << " ckpt/s\n"
              << "  Final state: " << (correct ? "CORRECT" : "CORRUPT") << "\n";

    REQUIRE(correct);
}

TEST_CASE("HACC kernel: multi-process file-per-process checkpoint", "[kernel][hacc]") {
    auto cfg = test_config();

    constexpr int num_ranks = 4; // Simulate 4 MPI ranks.
    constexpr int timesteps = 10;
    constexpr size_t ckpt_size = 128 * 1024; // 128KB per rank.

    // Each "rank" is a separate client instance (simulates separate processes).
    std::vector<labios::Client> ranks;
    for (int r = 0; r < num_ranks; ++r) {
        ranks.push_back(labios::connect(cfg));
    }

    auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < timesteps; ++t) {
        // All ranks checkpoint simultaneously.
        for (int r = 0; r < num_ranks; ++r) {
            std::vector<std::byte> state(ckpt_size);
            // Each rank's data is uniquely identified by rank + timestep.
            std::memset(state.data(), (r * 100 + t) & 0xFF, ckpt_size);

            std::string path = "/hacc/rank" + std::to_string(r) + "_ckpt.dat";
            ranks[r].write(path, state, 0);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    double total_mb = static_cast<double>(num_ranks * timesteps * ckpt_size) / (1024.0 * 1024.0);

    // Verify each rank's final checkpoint.
    int final_t = timesteps - 1;
    bool all_correct = true;
    for (int r = 0; r < num_ranks; ++r) {
        std::string path = "/hacc/rank" + std::to_string(r) + "_ckpt.dat";
        auto result = ranks[r].read(path, 0, ckpt_size);
        REQUIRE(result.size() == ckpt_size);

        auto expected = static_cast<std::byte>((r * 100 + final_t) & 0xFF);
        if (result[0] != expected || result[ckpt_size - 1] != expected) {
            all_correct = false;
            std::cerr << "  Rank " << r << ": expected 0x"
                      << std::hex << static_cast<int>(expected)
                      << ", got 0x" << static_cast<int>(result[0]) << std::dec << "\n";
        }
    }

    std::cout << "\n=== HACC Multi-Process Checkpoint ===\n"
              << "  Ranks: " << num_ranks << "\n"
              << "  Timesteps: " << timesteps << "\n"
              << "  Total data: " << total_mb << " MB\n"
              << "  Total time: " << total_sec << "s\n"
              << "  Throughput: " << (total_mb / total_sec) << " MB/s\n"
              << "  All ranks correct: " << (all_correct ? "YES" : "NO") << "\n";

    REQUIRE(all_correct);
}

TEST_CASE("HACC kernel: async checkpoint with deferred barrier", "[kernel][hacc]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 15;
    constexpr size_t ckpt_size = 256 * 1024;

    std::vector<std::byte> state(ckpt_size);
    labios::PendingIO prev_io;
    bool has_prev = false;

    auto t0 = std::chrono::steady_clock::now();

    for (int t = 0; t < timesteps; ++t) {
        // Wait for PREVIOUS checkpoint to complete before issuing new one.
        // This simulates the real HACC pattern where the app needs to know
        // the old checkpoint is durable before overwriting.
        if (has_prev) {
            client.wait(prev_io);
        }

        for (size_t i = 0; i < ckpt_size; ++i) {
            state[i] = static_cast<std::byte>((t + i) & 0xFF);
        }

        prev_io = client.async_write("/hacc/async_ckpt.dat", state, 0);
        has_prev = true;
    }

    // Final barrier.
    if (has_prev) client.wait(prev_io);

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();

    auto result = client.read("/hacc/async_ckpt.dat", 0, ckpt_size);
    REQUIRE(result.size() == ckpt_size);

    int final_t = timesteps - 1;
    bool correct = (result[0] == static_cast<std::byte>((final_t + 0) & 0xFF));

    std::cout << "\n=== HACC Async Checkpoint ===\n"
              << "  Timesteps: " << timesteps << "\n"
              << "  Total time: " << total_sec << "s\n"
              << "  Checkpoint rate: " << (timesteps / total_sec) << " ckpt/s\n"
              << "  Final state: " << (correct ? "CORRECT" : "CORRUPT") << "\n";

    REQUIRE(correct);
}
