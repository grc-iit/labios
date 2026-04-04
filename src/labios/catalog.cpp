#include <labios/catalog.h>

#include <chrono>
#include <stdexcept>
#include <string>

namespace labios {

namespace {

std::string now_ms() {
    auto now = std::chrono::system_clock::now();
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();
    return std::to_string(epoch_ms);
}

} // namespace

std::string to_string(LabelStatus status) {
    switch (status) {
        case LabelStatus::Queued:    return "queued";
        case LabelStatus::Scheduled: return "scheduled";
        case LabelStatus::Executing: return "executing";
        case LabelStatus::Complete:  return "complete";
        case LabelStatus::Error:     return "error";
    }
    return "unknown";
}

LabelStatus label_status_from_string(std::string_view s) {
    if (s == "queued")    return LabelStatus::Queued;
    if (s == "scheduled") return LabelStatus::Scheduled;
    if (s == "executing") return LabelStatus::Executing;
    if (s == "complete")  return LabelStatus::Complete;
    if (s == "error")     return LabelStatus::Error;
    throw std::invalid_argument(
        "unknown label status: " + std::string(s));
}

CatalogManager::CatalogManager(transport::RedisConnection& redis)
    : redis_(redis) {}

std::string CatalogManager::catalog_key(uint64_t label_id) const {
    return "labios:catalog:" + std::to_string(label_id);
}

void CatalogManager::create(uint64_t label_id, uint32_t app_id,
                             LabelType type) {
    auto key = catalog_key(label_id);
    auto ts = now_ms();
    redis_.hset(key, "status", "queued");
    redis_.hset(key, "app_id", std::to_string(app_id));
    redis_.hset(key, "type", std::to_string(static_cast<int>(type)));
    redis_.hset(key, "created_at", ts);
    redis_.hset(key, "updated_at", ts);
}

void CatalogManager::set_status(uint64_t label_id, LabelStatus status) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "status", to_string(status));
    redis_.hset(key, "updated_at", now_ms());
}

LabelStatus CatalogManager::get_status(uint64_t label_id) {
    auto key = catalog_key(label_id);
    auto val = redis_.hget(key, "status");
    if (!val.has_value()) {
        throw std::runtime_error(
            "catalog entry not found for label " + std::to_string(label_id));
    }
    return label_status_from_string(*val);
}

void CatalogManager::set_worker(uint64_t label_id, int worker_id) {
    redis_.hset(catalog_key(label_id), "worker_id",
                std::to_string(worker_id));
}

std::optional<int> CatalogManager::get_worker(uint64_t label_id) {
    auto val = redis_.hget(catalog_key(label_id), "worker_id");
    if (!val.has_value()) {
        return std::nullopt;
    }
    return std::stoi(*val);
}

std::string CatalogManager::location_key(std::string_view filepath) {
    return "labios:location:" + std::string(filepath);
}

void CatalogManager::set_location(std::string_view filepath, int worker_id) {
    redis_.set(location_key(filepath), std::to_string(worker_id));
}

std::optional<int> CatalogManager::get_location(std::string_view filepath) {
    auto val = redis_.get(location_key(filepath));
    if (!val.has_value()) {
        return std::nullopt;
    }
    return std::stoi(*val);
}

} // namespace labios
