#include <labios/config.h>

#include <toml++/toml.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace labios {

namespace {

std::string env_or(const char* name, const std::string& fallback) {
    const char* val = std::getenv(name);
    if (val != nullptr && val[0] != '\0') return val;
    return fallback;
}

int env_int_or(const char* name, int fallback) {
    const char* val = std::getenv(name);
    if (val != nullptr && val[0] != '\0') return std::stoi(val);
    return fallback;
}

} // namespace

Config load_config(const std::filesystem::path& path) {
    Config cfg;

    if (std::filesystem::exists(path)) {
        auto tbl = toml::parse_file(path.string());

        cfg.nats_url        = tbl["nats"]["url"].value_or(cfg.nats_url);
        cfg.redis_host      = tbl["redis"]["host"].value_or(cfg.redis_host);
        cfg.redis_port      = tbl["redis"]["port"].value_or(cfg.redis_port);
        cfg.worker_id       = tbl["worker"]["id"].value_or(cfg.worker_id);
        cfg.worker_speed    = tbl["worker"]["speed"].value_or(cfg.worker_speed);
        cfg.worker_capacity = tbl["worker"]["capacity"].value_or(cfg.worker_capacity);
    }

    cfg.nats_url        = env_or("LABIOS_NATS_URL", cfg.nats_url);
    cfg.redis_host      = env_or("LABIOS_REDIS_HOST", cfg.redis_host);
    cfg.redis_port      = env_int_or("LABIOS_REDIS_PORT", cfg.redis_port);
    cfg.worker_id       = env_int_or("LABIOS_WORKER_ID", cfg.worker_id);
    cfg.worker_speed    = env_int_or("LABIOS_WORKER_SPEED", cfg.worker_speed);
    cfg.worker_capacity = env_or("LABIOS_WORKER_CAPACITY", cfg.worker_capacity);

    return cfg;
}

} // namespace labios
