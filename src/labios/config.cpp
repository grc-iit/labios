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

uint64_t parse_size(std::string_view s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    double val = std::strtod(std::string(s).c_str(), &end);
    std::string_view suffix(end);
    if (suffix == "KB" || suffix == "kb") return static_cast<uint64_t>(val * 1024);
    if (suffix == "MB" || suffix == "mb") return static_cast<uint64_t>(val * 1024 * 1024);
    if (suffix == "GB" || suffix == "gb") return static_cast<uint64_t>(val * 1024 * 1024 * 1024);
    return static_cast<uint64_t>(val);
}

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

        if (auto v = tbl["label"]["min_size"].value<std::string>())
            cfg.label_min_size = parse_size(*v);
        if (auto v = tbl["label"]["max_size"].value<std::string>())
            cfg.label_max_size = parse_size(*v);
        cfg.cache_flush_interval_ms = tbl["cache"]["flush_interval_ms"].value_or(cfg.cache_flush_interval_ms);
        cfg.cache_read_policy = tbl["cache"]["default_read_policy"].value_or(cfg.cache_read_policy);
        if (auto arr = tbl["intercept"]["prefixes"].as_array()) {
            cfg.intercept_prefixes.clear();
            for (auto& elem : *arr) {
                if (auto s = elem.value<std::string>())
                    cfg.intercept_prefixes.push_back(*s);
            }
        }
        cfg.reply_timeout_ms = tbl["client"]["reply_timeout_ms"].value_or(cfg.reply_timeout_ms);
        cfg.dispatcher_batch_size = tbl["dispatcher"]["batch_size"].value_or(cfg.dispatcher_batch_size);
        cfg.dispatcher_batch_timeout_ms = tbl["dispatcher"]["batch_timeout_ms"].value_or(cfg.dispatcher_batch_timeout_ms);
        cfg.dispatcher_aggregation_enabled = tbl["dispatcher"]["aggregation_enabled"].value_or(cfg.dispatcher_aggregation_enabled);
        cfg.dispatcher_dep_granularity = tbl["dispatcher"]["dep_granularity"].value_or(cfg.dispatcher_dep_granularity);
        cfg.scheduler_policy = tbl["scheduler"]["policy"].value_or(cfg.scheduler_policy);
        cfg.scheduler_profile_path = tbl["scheduler"]["profile_path"].value_or(cfg.scheduler_profile_path);
        cfg.scheduler_worker_refresh_ms = tbl["scheduler"]["worker_refresh_ms"].value_or(cfg.scheduler_worker_refresh_ms);
        cfg.worker_energy = tbl["worker"]["energy"].value_or(cfg.worker_energy);
    }

    cfg.nats_url        = env_or("LABIOS_NATS_URL", cfg.nats_url);
    cfg.redis_host      = env_or("LABIOS_REDIS_HOST", cfg.redis_host);
    cfg.redis_port      = env_int_or("LABIOS_REDIS_PORT", cfg.redis_port);
    cfg.worker_id       = env_int_or("LABIOS_WORKER_ID", cfg.worker_id);
    cfg.worker_speed    = env_int_or("LABIOS_WORKER_SPEED", cfg.worker_speed);
    cfg.worker_capacity = env_or("LABIOS_WORKER_CAPACITY", cfg.worker_capacity);

    auto env_size = [](const char* name, uint64_t fallback) -> uint64_t {
        const char* val = std::getenv(name);
        if (val != nullptr && val[0] != '\0') return parse_size(val);
        return fallback;
    };
    cfg.label_min_size = env_size("LABIOS_LABEL_MIN_SIZE", cfg.label_min_size);
    cfg.label_max_size = env_size("LABIOS_LABEL_MAX_SIZE", cfg.label_max_size);
    cfg.cache_flush_interval_ms = env_int_or("LABIOS_CACHE_FLUSH_MS", cfg.cache_flush_interval_ms);
    cfg.cache_read_policy = env_or("LABIOS_CACHE_READ_POLICY", cfg.cache_read_policy);

    const char* prefixes_env = std::getenv("LABIOS_INTERCEPT_PREFIXES");
    if (prefixes_env != nullptr && prefixes_env[0] != '\0') {
        cfg.intercept_prefixes.clear();
        std::string_view sv(prefixes_env);
        size_t pos = 0;
        while (pos < sv.size()) {
            auto comma = sv.find(',', pos);
            if (comma == std::string_view::npos) comma = sv.size();
            auto token = sv.substr(pos, comma - pos);
            if (!token.empty()) {
                cfg.intercept_prefixes.emplace_back(token);
            }
            pos = comma + 1;
        }
    }

    cfg.reply_timeout_ms = env_int_or("LABIOS_REPLY_TIMEOUT_MS", cfg.reply_timeout_ms);
    cfg.dispatcher_batch_size = env_int_or("LABIOS_DISPATCHER_BATCH_SIZE", cfg.dispatcher_batch_size);
    cfg.dispatcher_batch_timeout_ms = env_int_or("LABIOS_DISPATCHER_BATCH_TIMEOUT_MS", cfg.dispatcher_batch_timeout_ms);
    {
        const char* agg = std::getenv("LABIOS_DISPATCHER_AGGREGATION");
        if (agg != nullptr && agg[0] != '\0') {
            std::string_view sv(agg);
            cfg.dispatcher_aggregation_enabled = (sv == "true" || sv == "1");
        }
    }
    cfg.dispatcher_dep_granularity = env_or("LABIOS_DISPATCHER_DEP_GRANULARITY", cfg.dispatcher_dep_granularity);

    cfg.scheduler_policy = env_or("LABIOS_SCHEDULER_POLICY", cfg.scheduler_policy);
    cfg.scheduler_profile_path = env_or("LABIOS_SCHEDULER_PROFILE", cfg.scheduler_profile_path);
    cfg.scheduler_worker_refresh_ms = env_int_or("LABIOS_SCHEDULER_WORKER_REFRESH_MS", cfg.scheduler_worker_refresh_ms);
    cfg.worker_energy = env_int_or("LABIOS_WORKER_ENERGY", cfg.worker_energy);

    return cfg;
}

WeightProfile load_weight_profile(const std::filesystem::path& path) {
    WeightProfile wp;
    wp.name = path.stem().string();
    if (!std::filesystem::exists(path)) return wp;

    auto tbl = toml::parse_file(path.string());
    wp.availability = tbl["weights"]["availability"].value_or(0.0);
    wp.capacity     = tbl["weights"]["capacity"].value_or(0.0);
    wp.load         = tbl["weights"]["load"].value_or(0.0);
    wp.speed        = tbl["weights"]["speed"].value_or(0.0);
    wp.energy       = tbl["weights"]["energy"].value_or(0.0);
    return wp;
}

} // namespace labios
