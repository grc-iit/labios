#include <labios/observability.h>

#include <labios/catalog_manager.h>

#include <chrono>
#include <sstream>
#include <unordered_set>

namespace labios {

namespace {

std::string now_ms() {
    auto tp = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count();
    return std::to_string(ms);
}

/// Minimal JSON helpers to avoid pulling in a library.
/// These produce compact JSON strings for observe responses.

std::string json_string(const std::string& key, const std::string& val) {
    return "\"" + key + "\":\"" + val + "\"";
}

std::string json_number(const std::string& key, uint64_t val) {
    return "\"" + key + "\":" + std::to_string(val);
}

std::string json_bool(const std::string& key, bool val) {
    return "\"" + key + "\":" + (val ? "true" : "false");
}

ObserveResult queue_depth(transport::RedisConnection& redis) {
    // Read the latest queue depth reported by the dispatcher to Redis.
    auto val = redis.get("labios:queue:depth");
    uint64_t depth = 0;
    if (val.has_value() && !val->empty()) {
        try { depth = std::stoull(*val); } catch (...) {}
    }
    std::string json = "{" + json_number("queue_depth", depth) + ","
                      + json_number("timestamp_ms", static_cast<uint64_t>(std::stoull(now_ms())))
                      + "}";
    return {true, {}, json};
}

ObserveResult workers_scores(const std::vector<WorkerInfo>& workers) {
    std::ostringstream oss;
    oss << "{\"workers\":[";
    for (size_t i = 0; i < workers.size(); ++i) {
        auto& w = workers[i];
        if (i > 0) oss << ",";
        oss << "{\"id\":" << w.id
            << ",\"tier\":" << static_cast<int>(w.tier)
            << ",\"available\":" << (w.available ? "true" : "false")
            << ",\"score\":" << w.score
            << ",\"capacity\":" << w.capacity
            << ",\"load\":" << w.load
            << ",\"speed\":" << w.speed
            << ",\"energy\":" << w.energy
            << "}";
    }
    oss << "]}";
    return {true, {}, oss.str()};
}

ObserveResult workers_count(const std::vector<WorkerInfo>& workers) {
    int databot = 0, pipeline = 0, agentic = 0;
    for (auto& w : workers) {
        switch (w.tier) {
        case WorkerTier::Databot:  ++databot;  break;
        case WorkerTier::Pipeline: ++pipeline; break;
        case WorkerTier::Agentic:  ++agentic;  break;
        }
    }
    int total = static_cast<int>(workers.size());
    std::string json = "{" + json_number("databot", static_cast<uint64_t>(databot)) + ","
                      + json_number("pipeline", static_cast<uint64_t>(pipeline)) + ","
                      + json_number("agentic", static_cast<uint64_t>(agentic)) + ","
                      + json_number("total", static_cast<uint64_t>(total)) + "}";
    return {true, {}, json};
}

ObserveResult system_health(transport::RedisConnection& redis,
                             transport::NatsConnection& nats) {
    std::string nats_status = nats.connected() ? "connected" : "disconnected";
    std::string redis_status = redis.connected() ? "connected" : "disconnected";

    // Read uptime if the dispatcher has stored its start time.
    uint64_t uptime_s = 0;
    auto start_val = redis.get("labios:dispatcher:start_ms");
    if (start_val.has_value() && !start_val->empty()) {
        try {
            uint64_t start_ms = std::stoull(*start_val);
            uint64_t now = static_cast<uint64_t>(std::stoull(now_ms()));
            uptime_s = (now - start_ms) / 1000;
        } catch (...) {}
    }

    std::string json = "{" + json_string("nats", nats_status) + ","
                      + json_string("redis", redis_status) + ","
                      + json_number("uptime_seconds", uptime_s) + "}";
    return {true, {}, json};
}

ObserveResult channels_list(transport::RedisConnection& redis) {
    // Channels use warehouse keys like "labios:channel:<name>:<seq>".
    // Scan for the pattern and extract unique channel names.
    auto keys = redis.scan_keys("labios:channel:*");
    std::unordered_set<std::string> names;
    for (auto& key : keys) {
        // key = "labios:channel:foo:123" → extract "foo"
        // Find third ':' to get name, then stop at fourth ':'.
        constexpr std::string_view prefix = "labios:channel:";
        if (key.size() <= prefix.size()) continue;
        auto rest = std::string_view(key).substr(prefix.size());
        auto colon = rest.find(':');
        if (colon != std::string_view::npos) {
            names.insert(std::string(rest.substr(0, colon)));
        } else {
            names.insert(std::string(rest));
        }
    }

    std::ostringstream oss;
    oss << "{\"channels\":[";
    bool first = true;
    for (auto& name : names) {
        if (!first) oss << ",";
        oss << "\"" << name << "\"";
        first = false;
    }
    oss << "]}";
    return {true, {}, oss.str()};
}

ObserveResult workspaces_list(transport::RedisConnection& redis) {
    // Workspaces use index keys like "labios:ws:<name>:_index".
    auto keys = redis.scan_keys("labios:ws:*:_index");
    std::vector<std::string> names;
    for (auto& key : keys) {
        constexpr std::string_view prefix = "labios:ws:";
        constexpr std::string_view suffix = ":_index";
        if (key.size() <= prefix.size() + suffix.size()) continue;
        auto rest = std::string_view(key).substr(prefix.size());
        if (rest.size() > suffix.size() &&
            rest.substr(rest.size() - suffix.size()) == suffix) {
            names.emplace_back(rest.substr(0, rest.size() - suffix.size()));
        }
    }

    std::ostringstream oss;
    oss << "{\"workspaces\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << names[i] << "\"";
    }
    oss << "]}";
    return {true, {}, oss.str()};
}

ObserveResult config_current(const Config& cfg) {
    std::ostringstream oss;
    oss << "{"
        << json_number("batch_size", static_cast<uint64_t>(cfg.dispatcher_batch_size)) << ","
        << json_number("batch_timeout_ms", static_cast<uint64_t>(cfg.dispatcher_batch_timeout_ms)) << ","
        << json_string("scheduler_policy", cfg.scheduler_policy) << ","
        << json_bool("aggregation_enabled", cfg.dispatcher_aggregation_enabled)
        << "}";
    return {true, {}, oss.str()};
}

ObserveResult data_location(const URI& query, CatalogManager& catalog) {
    // Parse "file" parameter from query string (e.g. "file=/data/checkpoint.pt").
    std::string file_path;
    std::string_view qs = query.query;
    while (!qs.empty()) {
        auto amp = qs.find('&');
        auto pair = qs.substr(0, amp);
        auto eq = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == "file") {
            file_path = pair.substr(eq + 1);
            break;
        }
        if (amp == std::string_view::npos) break;
        qs = qs.substr(amp + 1);
    }
    if (file_path.empty()) {
        return {false, "missing 'file' query parameter",
                "{\"error\":\"missing 'file' query parameter\"}"};
    }

    auto worker = catalog.get_location(file_path);
    auto file_info = catalog.get_file_info(file_path);

    std::ostringstream oss;
    oss << "{\"file\":\"" << file_path << "\"";
    if (worker.has_value()) {
        oss << ",\"worker_id\":" << *worker;
    } else {
        oss << ",\"worker_id\":null";
    }
    if (file_info.has_value()) {
        oss << ",\"exists\":" << (file_info->exists ? "true" : "false")
            << ",\"size\":" << file_info->size
            << ",\"mtime_ms\":" << file_info->mtime_ms;
    }
    oss << "}";
    return {true, {}, oss.str()};
}

} // anonymous namespace

ObserveResult handle_observe(const URI& query,
                              const std::vector<WorkerInfo>& workers,
                              transport::RedisConnection& redis,
                              transport::NatsConnection& nats,
                              const Config& cfg,
                              CatalogManager& catalog) {
    // Combine authority + path to form the routing key.
    // observe://queue/depth → authority="queue", path="/depth" → "queue/depth"
    std::string route = query.authority;
    if (!query.path.empty()) {
        if (query.path.front() == '/') {
            route += query.path;
        } else {
            route += "/" + query.path;
        }
    }

    if (route == "queue/depth")       return queue_depth(redis);
    if (route == "workers/scores")    return workers_scores(workers);
    if (route == "workers/count")     return workers_count(workers);
    if (route == "system/health")     return system_health(redis, nats);
    if (route == "channels/list")     return channels_list(redis);
    if (route == "workspaces/list")   return workspaces_list(redis);
    if (route == "config/current")    return config_current(cfg);
    if (route == "data/location")     return data_location(query, catalog);

    return {false, "unknown observe query: " + route,
            "{\"error\":\"unknown observe query: " + route + "\"}"};
}

} // namespace labios
