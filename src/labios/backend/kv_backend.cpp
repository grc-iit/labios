#include <labios/backend/kv_backend.h>
#include <labios/uri.h>

namespace labios {

KVBackend::KVBackend(transport::RedisConnection& redis, std::string prefix)
    : redis_(redis), prefix_(std::move(prefix)) {}

std::string KVBackend::make_key(const LabelData& label) const {
    std::string uri = !label.dest_uri.empty() ? label.dest_uri : label.source_uri;
    if (!uri.empty()) {
        auto parsed = parse_uri(uri);
        if (!parsed.authority.empty()) {
            return prefix_ + parsed.authority + parsed.path;
        }
        return prefix_ + parsed.path;
    }
    return prefix_ + std::to_string(label.id);
}

BackendResult KVBackend::put(const LabelData& label,
                              std::span<const std::byte> data) {
    auto key = make_key(label);
    redis_.set_binary(key, data);

    std::string meta_key = key + ":meta";
    redis_.hset(meta_key, "intent", std::to_string(static_cast<int>(label.intent)));
    redis_.hset(meta_key, "isolation", std::to_string(static_cast<int>(label.isolation)));
    redis_.hset(meta_key, "priority", std::to_string(label.priority));

    if (label.ttl_seconds > 0) {
        redis_.expire(key, label.ttl_seconds);
        redis_.expire(meta_key, label.ttl_seconds);
    }
    return {};
}

BackendDataResult KVBackend::get(const LabelData& label) {
    auto key = make_key(label);
    auto data = redis_.get_binary(key);
    if (data.empty()) {
        return {false, "key not found: " + key, {}};
    }
    return {true, {}, std::move(data)};
}

BackendResult KVBackend::del(const LabelData& label) {
    auto key = make_key(label);
    redis_.del(key);
    redis_.del(key + ":meta");
    return {};
}

BackendQueryResult KVBackend::query(const LabelData& label) {
    auto key = make_key(label);
    auto keys = redis_.scan_keys(key + "*");
    std::string json = "{\"keys\":[";
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + keys[i] + "\"";
    }
    json += "]}";
    return {true, {}, json};
}

} // namespace labios
