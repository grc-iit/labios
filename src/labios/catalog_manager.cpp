#include <labios/catalog_manager.h>

#include <chrono>
#include <fcntl.h>
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
    LabelData label;
    label.id = label_id;
    label.type = type;
    label.app_id = app_id;
    create(label);
}

void CatalogManager::create(const LabelData& label) {
    auto key = catalog_key(label.id);
    auto ts = now_ms();
    redis_.hset(key, "status", "queued");
    redis_.hset(key, "app_id", std::to_string(label.app_id));
    redis_.hset(key, "type", std::to_string(static_cast<int>(label.type)));
    redis_.hset(key, "flags", std::to_string(label.flags));
    redis_.hset(key, "priority", std::to_string(label.priority));
    redis_.hset(key, "operation", label.operation);
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

void CatalogManager::set_flags(uint64_t label_id, uint32_t flags) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "flags", std::to_string(flags));
    redis_.hset(key, "updated_at", now_ms());
}

uint32_t CatalogManager::get_flags(uint64_t label_id) {
    auto key = catalog_key(label_id);
    auto val = redis_.hget(key, "flags");
    if (!val.has_value()) {
        throw std::runtime_error(
            "catalog flags not found for label " + std::to_string(label_id));
    }
    return static_cast<uint32_t>(std::stoul(*val));
}

void CatalogManager::set_error(uint64_t label_id, std::string_view error) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "error", std::string(error));
    redis_.hset(key, "updated_at", now_ms());
}

std::optional<std::string> CatalogManager::get_error(uint64_t label_id) {
    return redis_.hget(catalog_key(label_id), "error");
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

std::string CatalogManager::filemeta_key(std::string_view filepath) {
    return "labios:filemeta:" + std::string(filepath);
}

void CatalogManager::track_open(std::string_view filepath, int flags) {
    auto key = filemeta_key(filepath);
    if (flags & O_CREAT) {
        redis_.hset(key, "exists", "1");
        auto existing = redis_.hget(key, "size");
        if (!existing.has_value()) {
            redis_.hset(key, "size", "0");
        }
        redis_.hset(key, "mtime", now_ms());
    }
    if (flags & O_TRUNC) {
        redis_.hset(key, "exists", "1");
        redis_.hset(key, "size", "0");
        redis_.hset(key, "mtime", now_ms());
    }
}

void CatalogManager::track_write(std::string_view filepath,
                                  uint64_t offset, uint64_t size) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "1");
    uint64_t end = offset + size;
    auto cur = redis_.hget(key, "size");
    uint64_t cur_size = cur.has_value() ? std::stoull(*cur) : 0;
    if (end > cur_size) {
        redis_.hset(key, "size", std::to_string(end));
    }
    redis_.hset(key, "mtime", now_ms());
}

void CatalogManager::track_unlink(std::string_view filepath) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "0");
    redis_.hset(key, "size", "0");
    redis_.hset(key, "mtime", now_ms());
    redis_.del(location_key(filepath));
}

void CatalogManager::track_truncate(std::string_view filepath,
                                     uint64_t new_size) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "1");
    redis_.hset(key, "size", std::to_string(new_size));
    redis_.hset(key, "mtime", now_ms());
}

std::optional<FileInfo> CatalogManager::get_file_info(std::string_view filepath) {
    auto key = filemeta_key(filepath);
    auto exists_val = redis_.hget(key, "exists");
    if (!exists_val.has_value()) {
        return std::nullopt;
    }
    FileInfo info;
    info.exists = (*exists_val == "1");
    auto size_val = redis_.hget(key, "size");
    if (size_val.has_value()) {
        info.size = std::stoull(*size_val);
    }
    auto mtime_val = redis_.hget(key, "mtime");
    if (mtime_val.has_value()) {
        info.mtime_ms = std::stoull(*mtime_val);
    }
    return info;
}

} // namespace labios
