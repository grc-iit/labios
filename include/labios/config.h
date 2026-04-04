#pragma once

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
    uint64_t label_min_size = 64 * 1024;       // 64KB - below this, writes go to cache
    uint64_t label_max_size = 1024 * 1024;     // 1MB - above this, writes get split

    // Small-I/O cache (Content Manager)
    int cache_flush_interval_ms = 500;
    std::string cache_read_policy = "read-through"; // or "write-only"

    // POSIX intercept
    std::vector<std::string> intercept_prefixes = {"/labios"};
};

/// Load config from TOML file. Environment variables override file values.
/// Env vars: LABIOS_NATS_URL, LABIOS_REDIS_HOST, LABIOS_REDIS_PORT,
///           LABIOS_WORKER_ID, LABIOS_WORKER_SPEED, LABIOS_WORKER_CAPACITY
Config load_config(const std::filesystem::path& path);

/// Parse a human-readable size string (e.g. "64KB", "1MB", "2GB") into bytes.
uint64_t parse_size(std::string_view s);

} // namespace labios
