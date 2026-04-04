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

#include <algorithm>
#include <chrono>
#include <cmath>
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

// --- Benchmark helpers ---

constexpr int default_trials = 5;
constexpr int default_warmup = 1;

struct TrialResult {
    double elapsed_sec;
    double throughput_mbps;
};

static std::vector<TrialResult> run_trials(
    labios::Client& client,
    int num_trials,
    const std::string& prefix,
    size_t output_size,
    int timesteps,
    int compute_ms,
    bool use_async,
    int barrier_interval = 0)
{
    std::vector<TrialResult> results;
    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    for (int trial = 0; trial < num_trials; ++trial) {
        std::vector<std::byte> grid(output_size);
        std::iota(reinterpret_cast<uint8_t*>(grid.data()),
                  reinterpret_cast<uint8_t*>(grid.data()) + output_size,
                  static_cast<uint8_t>(0));

        auto t0 = std::chrono::steady_clock::now();

        if (!use_async) {
            for (int t = 0; t < timesteps; ++t) {
                simulate_compute(compute_ms);
                std::string path = prefix + "t" + std::to_string(trial) +
                    "_s" + std::to_string(t) + ".dat";
                grid[0] = static_cast<std::byte>(t);
                client.write(path, grid);
            }
        } else if (barrier_interval <= 0) {
            std::vector<labios::PendingIO> pending;
            for (int t = 0; t < timesteps; ++t) {
                simulate_compute(compute_ms);
                std::string path = prefix + "t" + std::to_string(trial) +
                    "_s" + std::to_string(t) + ".dat";
                grid[0] = static_cast<std::byte>(t);
                pending.push_back(client.async_write(path, grid));
            }
            for (auto& p : pending) client.wait(p);
        } else {
            std::vector<labios::PendingIO> batch;
            for (int t = 0; t < timesteps; ++t) {
                simulate_compute(compute_ms);
                std::string path = prefix + "t" + std::to_string(trial) +
                    "_s" + std::to_string(t) + ".dat";
                grid[0] = static_cast<std::byte>(t);
                batch.push_back(client.async_write(path, grid));
                if ((t + 1) % barrier_interval == 0) {
                    for (auto& p : batch) client.wait(p);
                    batch.clear();
                }
            }
            for (auto& p : batch) client.wait(p);
        }

        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        results.push_back({elapsed, data_mb / elapsed});
    }
    return results;
}

static void print_stats(const std::string& label,
                         const std::vector<TrialResult>& results,
                         int warmup = default_warmup) {
    size_t start = static_cast<size_t>(warmup);
    if (start >= results.size()) start = 0;
    size_t n = results.size() - start;

    double sum_t = 0, sum_tp = 0;
    double min_t = 1e30, max_t = 0;
    for (size_t i = start; i < results.size(); ++i) {
        sum_t += results[i].elapsed_sec;
        sum_tp += results[i].throughput_mbps;
        min_t = std::min(min_t, results[i].elapsed_sec);
        max_t = std::max(max_t, results[i].elapsed_sec);
    }
    double mean_t = sum_t / static_cast<double>(n);
    double mean_tp = sum_tp / static_cast<double>(n);

    double var = 0;
    for (size_t i = start; i < results.size(); ++i) {
        double d = results[i].elapsed_sec - mean_t;
        var += d * d;
    }
    double stddev = std::sqrt(var / static_cast<double>(n));

    std::cout << "\n=== " << label << " ===\n"
              << "  Trials: " << results.size() << " (warmup: " << warmup << ")\n"
              << "  Time:   " << mean_t << "s  (stddev=" << stddev
              << ", min=" << min_t << ", max=" << max_t << ")\n"
              << "  Throughput: " << mean_tp << " MB/s\n";
}

static double mean_elapsed(const std::vector<TrialResult>& results, int warmup) {
    double sum = 0;
    int n = 0;
    for (size_t i = static_cast<size_t>(warmup); i < results.size(); ++i) {
        sum += results[i].elapsed_sec;
        ++n;
    }
    return sum / n;
}

// --- Test cases ---

TEST_CASE("CM1 kernel: sync write burst", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;

    auto results = run_trials(client, default_trials, "/cm1/sync/",
                              output_size, timesteps, compute_ms, false);
    print_stats("CM1 Sync Mode", results);

    // Verify last trial, last timestep.
    int last_trial = default_trials - 1;
    std::string path = "/cm1/sync/t" + std::to_string(last_trial) +
        "_s" + std::to_string(timesteps - 1) + ".dat";
    auto result = client.read(path, 0, output_size);
    REQUIRE(result.size() == output_size);
    CHECK(result[0] == static_cast<std::byte>(timesteps - 1));
}

TEST_CASE("CM1 kernel: async write burst", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;

    auto results = run_trials(client, default_trials, "/cm1/async/",
                              output_size, timesteps, compute_ms, true);
    print_stats("CM1 Async Mode", results);

    int last_trial = default_trials - 1;
    std::string path = "/cm1/async/t" + std::to_string(last_trial) +
        "_s" + std::to_string(timesteps - 1) + ".dat";
    auto result = client.read(path, 0, output_size);
    REQUIRE(result.size() == output_size);
    CHECK(result[0] == static_cast<std::byte>(timesteps - 1));
}

TEST_CASE("CM1 kernel: async pipelined with periodic barriers", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;
    constexpr int barrier_interval = 3;

    auto results = run_trials(client, default_trials, "/cm1/pipelined/",
                              output_size, timesteps, compute_ms, true,
                              barrier_interval);
    print_stats("CM1 Pipelined Mode (barrier every 3)", results);

    // Verify all timesteps from last trial.
    int last_trial = default_trials - 1;
    for (int t = 0; t < timesteps; ++t) {
        std::string path = "/cm1/pipelined/t" + std::to_string(last_trial) +
            "_s" + std::to_string(t) + ".dat";
        auto result = client.read(path, 0, output_size);
        REQUIRE(result.size() == output_size);
        CHECK(result[0] == static_cast<std::byte>(t));
    }
}

TEST_CASE("CM1 kernel: mode comparison summary", "[kernel][cm1]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int trials = default_trials;
    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;

    auto sync_r = run_trials(client, trials, "/cm1/cmp/sync/", output_size,
                              timesteps, compute_ms, false);
    auto async_r = run_trials(client, trials, "/cm1/cmp/async/", output_size,
                               timesteps, compute_ms, true);
    auto pipe_r = run_trials(client, trials, "/cm1/cmp/pipe/", output_size,
                              timesteps, compute_ms, true, 3);

    print_stats("Sync", sync_r);
    print_stats("Async", async_r);
    print_stats("Pipelined (barrier=3)", pipe_r);

    double sync_mean = mean_elapsed(sync_r, default_warmup);
    double async_mean = mean_elapsed(async_r, default_warmup);
    double pipe_mean = mean_elapsed(pipe_r, default_warmup);

    std::cout << "\n  Async speedup over sync: " << (sync_mean / async_mean) << "x\n"
              << "  Pipelined speedup over sync: " << (sync_mean / pipe_mean) << "x\n";

    // Async should not be slower than sync (sanity check).
    CHECK(async_mean <= sync_mean * 1.5);
}
