#pragma once

#include <labios/solver/solver.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

struct Config {
    std::string nats_url = "nats://localhost:4222";
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int worker_id = 0;
    int worker_speed = 1;
    std::string worker_capacity = "1GB";
    std::string service_name;

    // Label granularity (paper Section 2.2)
    uint64_t label_min_size = 64 * 1024;       // 64KB, writes below go to cache
    uint64_t label_max_size = 1024 * 1024;     // 1MB, writes above get split

    // Small-I/O cache (Content Manager)
    int cache_flush_interval_ms = 500;
    std::string cache_read_policy = "read-through"; // or "write-only"

    // POSIX intercept
    std::vector<std::string> intercept_prefixes = {"/labios"};

    // Client
    int reply_timeout_ms = 30000;

    // Dispatcher / Shuffler (paper Section 2.3)
    int dispatcher_batch_size = 100;
    int dispatcher_batch_timeout_ms = 50;
    bool dispatcher_aggregation_enabled = true;
    std::string dispatcher_dep_granularity = "per-file";

    // Scheduler (paper Section 2.4)
    std::string scheduler_policy = "round-robin";      // round-robin, random, constraint, minmax
    std::string scheduler_profile_path;                 // path to weight profile TOML
    int scheduler_worker_refresh_ms = 5000;             // worker list cache refresh interval

    // Worker energy (paper Section 2.5)
    int worker_energy = 1;  // [1,5] power wattage class
};

/// Load config from TOML file. Environment variables override file values.
/// Env vars: LABIOS_NATS_URL, LABIOS_REDIS_HOST, LABIOS_REDIS_PORT,
///           LABIOS_WORKER_ID, LABIOS_WORKER_SPEED, LABIOS_WORKER_CAPACITY
Config load_config(const std::filesystem::path& path);

/// Parse a human-readable size string (e.g. "64KB", "1MB", "2GB") into bytes.
uint64_t parse_size(std::string_view s);

/// Load a weight profile from a TOML file.
WeightProfile load_weight_profile(const std::filesystem::path& path);

} // namespace labios
