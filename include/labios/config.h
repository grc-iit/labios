#pragma once

#include <filesystem>
#include <string>

namespace labios {

struct Config {
    std::string nats_url = "nats://localhost:4222";
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int worker_id = 0;
    int worker_speed = 1;
    std::string worker_capacity = "1GB";
    std::string service_name;
};

/// Load config from TOML file. Environment variables override file values.
/// Env vars: LABIOS_NATS_URL, LABIOS_REDIS_HOST, LABIOS_REDIS_PORT,
///           LABIOS_WORKER_ID, LABIOS_WORKER_SPEED, LABIOS_WORKER_CAPACITY
Config load_config(const std::filesystem::path& path);

} // namespace labios
