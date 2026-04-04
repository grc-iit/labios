// tests/integration/kernel_cm1_test.cpp
//
// CM1 Application Kernel: Sequential Write-Intensive Bursts
//
// Paper reference: HPDC'19 Section 2.2(a), Section 4 (16x speedup)
// Pattern: periodic sequential writes (atmospheric simulation output)
// Goal: demonstrate async I/O throughput advantage over sync,
//       with filesystem baseline and I/O vs compute separation

#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    double total_sec;       // Wall clock including compute
    double io_sec;          // Just the I/O portion
    double compute_sec;     // Just the compute portion
    double throughput_mbps;  // data_mb / io_sec (pure I/O throughput)
    double effective_mbps;   // data_mb / total_sec (application-visible throughput)
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

        double compute_acc = 0;
        double io_acc = 0;
        auto t0 = std::chrono::steady_clock::now();

        if (!use_async) {
            // Sync: measure compute and I/O separately.
            for (int t = 0; t < timesteps; ++t) {
                auto c0 = std::chrono::steady_clock::now();
                simulate_compute(compute_ms);
                auto c1 = std::chrono::steady_clock::now();
                compute_acc += std::chrono::duration<double>(c1 - c0).count();

                std::string path = prefix + "t" + std::to_string(trial) +
                    "_s" + std::to_string(t) + ".dat";
                auto i0 = std::chrono::steady_clock::now();
                grid[0] = static_cast<std::byte>(t);
                client.write(path, grid);
                auto i1 = std::chrono::steady_clock::now();
                io_acc += std::chrono::duration<double>(i1 - i0).count();
            }
        } else if (barrier_interval <= 0) {
            // Async: compute and I/O overlap. I/O stall = total - compute.
            std::vector<labios::PendingIO> pending;
            for (int t = 0; t < timesteps; ++t) {
                auto c0 = std::chrono::steady_clock::now();
                simulate_compute(compute_ms);
                auto c1 = std::chrono::steady_clock::now();
                compute_acc += std::chrono::duration<double>(c1 - c0).count();

                std::string path = prefix + "t" + std::to_string(trial) +
                    "_s" + std::to_string(t) + ".dat";
                grid[0] = static_cast<std::byte>(t);
                pending.push_back(client.async_write(path, grid));
            }
            for (auto& p : pending) client.wait(p);
        } else {
            // Pipelined: same overlap model as async.
            std::vector<labios::PendingIO> batch;
            for (int t = 0; t < timesteps; ++t) {
                auto c0 = std::chrono::steady_clock::now();
                simulate_compute(compute_ms);
                auto c1 = std::chrono::steady_clock::now();
                compute_acc += std::chrono::duration<double>(c1 - c0).count();

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
        double total = std::chrono::duration<double>(t1 - t0).count();

        // For async/pipelined modes, I/O stall is total minus compute.
        if (use_async) {
            io_acc = total - compute_acc;
        }

        double io_tp = (io_acc > 0) ? data_mb / io_acc : 0;
        double eff_tp = (total > 0) ? data_mb / total : 0;
        results.push_back({total, io_acc, compute_acc, io_tp, eff_tp});
    }
    return results;
}

static void print_stats(const std::string& label,
                         const std::vector<TrialResult>& results,
                         int warmup = default_warmup) {
    size_t start = static_cast<size_t>(warmup);
    if (start >= results.size()) start = 0;
    size_t n = results.size() - start;

    double sum_total = 0, sum_io = 0, sum_compute = 0;
    double sum_tp = 0, sum_eff = 0;
    double min_t = 1e30, max_t = 0;
    for (size_t i = start; i < results.size(); ++i) {
        sum_total += results[i].total_sec;
        sum_io += results[i].io_sec;
        sum_compute += results[i].compute_sec;
        sum_tp += results[i].throughput_mbps;
        sum_eff += results[i].effective_mbps;
        min_t = std::min(min_t, results[i].total_sec);
        max_t = std::max(max_t, results[i].total_sec);
    }
    double nd = static_cast<double>(n);
    double mean_total = sum_total / nd;
    double mean_io = sum_io / nd;
    double mean_compute = sum_compute / nd;
    double mean_tp = sum_tp / nd;
    double mean_eff = sum_eff / nd;

    double var = 0;
    for (size_t i = start; i < results.size(); ++i) {
        double d = results[i].total_sec - mean_total;
        var += d * d;
    }
    double stddev = std::sqrt(var / nd);

    double io_overhead_pct = (mean_compute > 0)
        ? (mean_io / mean_compute) * 100.0
        : 0;

    std::cout << "\n=== " << label << " ===\n"
              << "  Trials: " << results.size() << " (warmup: " << warmup << ")\n"
              << "  Total wall time:     " << mean_total << "s  (stddev=" << stddev
              << ", min=" << min_t << ", max=" << max_t << ")\n"
              << "  Compute time:        " << mean_compute << "s\n"
              << "  I/O time:            " << mean_io << "s\n"
              << "  Pure I/O throughput:  " << mean_tp << " MB/s\n"
              << "  Effective throughput: " << mean_eff << " MB/s (includes compute)\n";
    if (mean_compute > 0) {
        std::cout << "  I/O overhead:        " << mean_io << "s ("
                  << io_overhead_pct << "% of compute)\n";
    }
}

// --- Test cases ---

TEST_CASE("CM1 baseline: direct filesystem write", "[kernel][cm1][baseline]") {
    constexpr int trials = 5;
    constexpr int warmup = 1;
    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024; // 1MB
    constexpr int compute_ms = 50;

    std::vector<std::byte> grid(output_size);
    std::iota(reinterpret_cast<uint8_t*>(grid.data()),
              reinterpret_cast<uint8_t*>(grid.data()) + output_size,
              static_cast<uint8_t>(0));

    std::vector<TrialResult> results;
    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    for (int trial = 0; trial < trials; ++trial) {
        double compute_acc = 0, io_acc = 0;
        auto t0 = std::chrono::steady_clock::now();

        for (int t = 0; t < timesteps; ++t) {
            auto c0 = std::chrono::steady_clock::now();
            simulate_compute(compute_ms);
            auto c1 = std::chrono::steady_clock::now();
            compute_acc += std::chrono::duration<double>(c1 - c0).count();

            std::string path = "/tmp/cm1_baseline_t" + std::to_string(trial) +
                "_s" + std::to_string(t) + ".dat";
            auto i0 = std::chrono::steady_clock::now();
            std::ofstream ofs(path, std::ios::binary);
            grid[0] = static_cast<std::byte>(t);
            ofs.write(reinterpret_cast<const char*>(grid.data()),
                      static_cast<std::streamsize>(output_size));
            ofs.flush();  // Force to kernel buffer (not fsync).
            auto i1 = std::chrono::steady_clock::now();
            io_acc += std::chrono::duration<double>(i1 - i0).count();
        }

        auto t1 = std::chrono::steady_clock::now();
        double total = std::chrono::duration<double>(t1 - t0).count();
        results.push_back({total, io_acc, compute_acc,
                           data_mb / io_acc, data_mb / total});
    }

    print_stats("Direct Filesystem (fwrite)", results, warmup);

    // Clean up temp files.
    for (int trial = 0; trial < trials; ++trial) {
        for (int t = 0; t < timesteps; ++t) {
            std::string path = "/tmp/cm1_baseline_t" + std::to_string(trial) +
                "_s" + std::to_string(t) + ".dat";
            std::filesystem::remove(path);
        }
    }
}

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

TEST_CASE("CM1 kernel: high-throughput 1000 labels", "[kernel][cm1][scale]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int trials = 3;
    constexpr int warmup = 1;
    constexpr int timesteps = 1000;
    constexpr size_t output_size = 64 * 1024; // 64KB per label
    // No compute phase: pure I/O stress test.

    std::vector<std::byte> grid(output_size);
    std::iota(reinterpret_cast<uint8_t*>(grid.data()),
              reinterpret_cast<uint8_t*>(grid.data()) + output_size,
              static_cast<uint8_t>(0));

    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    // Sync mode at scale.
    std::vector<TrialResult> sync_r;
    for (int trial = 0; trial < trials; ++trial) {
        auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < timesteps; ++t) {
            std::string path = "/cm1/scale/sync/t" + std::to_string(trial) +
                "_s" + std::to_string(t) + ".dat";
            grid[0] = static_cast<std::byte>(t & 0xFF);
            client.write(path, grid);
        }
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        sync_r.push_back({elapsed, elapsed, 0.0, data_mb / elapsed, data_mb / elapsed});
    }

    // Async mode at scale.
    std::vector<TrialResult> async_r;
    for (int trial = 0; trial < trials; ++trial) {
        std::vector<labios::PendingIO> pending;
        auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < timesteps; ++t) {
            std::string path = "/cm1/scale/async/t" + std::to_string(trial) +
                "_s" + std::to_string(t) + ".dat";
            grid[0] = static_cast<std::byte>(t & 0xFF);
            pending.push_back(client.async_write(path, grid));
        }
        for (auto& p : pending) client.wait(p);
        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        async_r.push_back({elapsed, elapsed, 0.0, data_mb / elapsed, data_mb / elapsed});
    }

    print_stats("1000-label Sync (pure I/O)", sync_r, warmup);
    print_stats("1000-label Async (pure I/O)", async_r, warmup);

    auto mean_io = [](const std::vector<TrialResult>& r, int w) {
        double sum = 0; int n = 0;
        for (size_t i = static_cast<size_t>(w); i < r.size(); ++i) {
            sum += r[i].io_sec; ++n;
        }
        return sum / n;
    };

    std::cout << "\n  Labels: " << timesteps << " x " << (output_size / 1024) << "KB = "
              << data_mb << " MB\n"
              << "  Async speedup: " << (mean_io(sync_r, 1) / mean_io(async_r, 1)) << "x\n"
              << "  Labels/sec (sync):  " << (timesteps / mean_io(sync_r, 1)) << "\n"
              << "  Labels/sec (async): " << (timesteps / mean_io(async_r, 1)) << "\n";
}

TEST_CASE("CM1 kernel: full comparison table", "[kernel][cm1][summary]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int trials = 5;
    constexpr int timesteps = 10;
    constexpr size_t output_size = 1024 * 1024;
    constexpr int compute_ms = 50;
    double data_mb = static_cast<double>(timesteps * output_size) / (1024.0 * 1024.0);

    // Run all LABIOS modes.
    auto sync_r = run_trials(client, trials, "/cm1/final/sync/", output_size,
                              timesteps, compute_ms, false);
    auto async_r = run_trials(client, trials, "/cm1/final/async/", output_size,
                               timesteps, compute_ms, true);
    auto pipe_r = run_trials(client, trials, "/cm1/final/pipe/", output_size,
                              timesteps, compute_ms, true, 3);

    // Direct filesystem baseline.
    std::vector<std::byte> grid(output_size);
    std::iota(reinterpret_cast<uint8_t*>(grid.data()),
              reinterpret_cast<uint8_t*>(grid.data()) + output_size,
              static_cast<uint8_t>(0));

    std::vector<TrialResult> baseline_r;
    for (int trial = 0; trial < trials; ++trial) {
        double compute_acc = 0, io_acc = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < timesteps; ++t) {
            auto c0 = std::chrono::steady_clock::now();
            simulate_compute(compute_ms);
            auto c1 = std::chrono::steady_clock::now();
            compute_acc += std::chrono::duration<double>(c1 - c0).count();

            std::string path = "/tmp/cm1_final_t" + std::to_string(trial) +
                "_s" + std::to_string(t) + ".dat";
            auto i0 = std::chrono::steady_clock::now();
            std::ofstream ofs(path, std::ios::binary);
            grid[0] = static_cast<std::byte>(t);
            ofs.write(reinterpret_cast<const char*>(grid.data()),
                      static_cast<std::streamsize>(output_size));
            ofs.flush();
            auto i1 = std::chrono::steady_clock::now();
            io_acc += std::chrono::duration<double>(i1 - i0).count();
        }
        auto t1 = std::chrono::steady_clock::now();
        double total = std::chrono::duration<double>(t1 - t0).count();
        baseline_r.push_back({total, io_acc, compute_acc,
                              data_mb / io_acc, data_mb / total});
        for (int t = 0; t < timesteps; ++t) {
            std::filesystem::remove("/tmp/cm1_final_t" + std::to_string(trial) +
                "_s" + std::to_string(t) + ".dat");
        }
    }

    std::cout << "\n╔═══════════════════════════════════════════════════════════════════════╗\n"
              << "║               CM1 COMPREHENSIVE COMPARISON (10 x 1MB)                ║\n"
              << "╠═══════════════════════════════════════════════════════════════════════╣\n";
    print_stats("Direct Filesystem (baseline)", baseline_r);
    print_stats("LABIOS Sync", sync_r);
    print_stats("LABIOS Async", async_r);
    print_stats("LABIOS Pipelined (barrier=3)", pipe_r);

    auto mean_total = [](const std::vector<TrialResult>& r, int w) {
        double sum = 0; int n = 0;
        for (size_t i = static_cast<size_t>(w); i < r.size(); ++i) {
            sum += r[i].total_sec; ++n;
        }
        return sum / n;
    };

    double base_t = mean_total(baseline_r, 1);
    double sync_t = mean_total(sync_r, 1);
    double async_t = mean_total(async_r, 1);
    double pipe_t = mean_total(pipe_r, 1);

    std::cout << "\n  Wall clock comparison (lower is better):\n"
              << "    Baseline:     " << base_t << "s\n"
              << "    LABIOS Sync:  " << sync_t << "s ("
              << (sync_t > base_t ? "+" : "") << ((sync_t / base_t - 1) * 100) << "%)\n"
              << "    LABIOS Async: " << async_t << "s ("
              << (async_t > base_t ? "+" : "") << ((async_t / base_t - 1) * 100) << "%)\n"
              << "    LABIOS Pipe:  " << pipe_t << "s ("
              << (pipe_t > base_t ? "+" : "") << ((pipe_t / base_t - 1) * 100) << "%)\n"
              << "\n  Key insight: async mode's wall time approaches compute-only time\n"
              << "  (0.5s) because I/O is hidden behind compute.\n"
              << "╚═══════════════════════════════════════════════════════════════════════╝\n";
}
