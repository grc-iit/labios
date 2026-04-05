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

    // Worker tier (LABIOS-SPEC Section 3)
    int worker_tier = 0;    // 0=Databot, 1=Pipeline, 2=Agentic

    // Manager: max worker capacity for normalization (default 1TB)
    uint64_t max_worker_capacity = 1024ULL * 1024 * 1024 * 1024;

    // Elastic worker management (paper Section 2.8, M4)
    struct ElasticConfig {
        bool enabled = false;
        int min_workers = 1;
        int max_workers = 10;
        int pressure_threshold = 5;            // Consecutive full batches before commission
        int worker_idle_timeout_ms = 30000;    // 30s idle before worker self-suspends
        int decommission_timeout_ms = 60000;   // 60s suspended before container removed
        int commission_cooldown_ms = 5000;     // 5s minimum between commissions
        int eval_interval_ms = 2000;           // How often the orchestrator evaluates
        std::string docker_socket = "/var/run/docker.sock";
        std::string docker_image;              // Worker Docker image name
        std::string docker_network;            // Docker network for new containers
        int elastic_worker_speed = 3;          // Default speed class [1,5]
        int elastic_worker_energy = 3;         // Default energy class [1,5]
        std::string elastic_worker_capacity = "50GB";
    };

    ElasticConfig elastic;
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
