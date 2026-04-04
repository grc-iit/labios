#pragma once

#include <labios/label.h>
#include <labios/transport/redis.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace labios {

enum class LabelStatus : uint8_t { Queued, Scheduled, Executing, Complete, Error };

std::string to_string(LabelStatus status);
LabelStatus label_status_from_string(std::string_view s);

struct FileInfo {
    uint64_t size = 0;
    uint64_t mtime_ms = 0;
    bool exists = false;
};

class CatalogManager {
public:
    explicit CatalogManager(transport::RedisConnection& redis);

    void create(uint64_t label_id, uint32_t app_id, LabelType type);
    void create(const LabelData& label);
    void set_status(uint64_t label_id, LabelStatus status);
    LabelStatus get_status(uint64_t label_id);
    void set_flags(uint64_t label_id, uint32_t flags);
    uint32_t get_flags(uint64_t label_id);
    void set_error(uint64_t label_id, std::string_view error);
    std::optional<std::string> get_error(uint64_t label_id);
    void set_worker(uint64_t label_id, int worker_id);
    std::optional<int> get_worker(uint64_t label_id);

    /// Track which worker holds data for a given filepath.
    void set_location(std::string_view filepath, int worker_id);
    std::optional<int> get_location(std::string_view filepath);

    void track_open(std::string_view filepath, int flags);
    void track_write(std::string_view filepath, uint64_t offset, uint64_t size);
    void track_unlink(std::string_view filepath);
    void track_truncate(std::string_view filepath, uint64_t new_size);
    std::optional<FileInfo> get_file_info(std::string_view filepath);

private:
    transport::RedisConnection& redis_;
    std::string catalog_key(uint64_t label_id) const;
    static std::string location_key(std::string_view filepath);
    static std::string filemeta_key(std::string_view filepath);
};

} // namespace labios
