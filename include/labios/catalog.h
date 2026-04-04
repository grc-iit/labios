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

class CatalogManager {
public:
    explicit CatalogManager(transport::RedisConnection& redis);

    void create(uint64_t label_id, uint32_t app_id, LabelType type);
    void set_status(uint64_t label_id, LabelStatus status);
    LabelStatus get_status(uint64_t label_id);
    void set_worker(uint64_t label_id, int worker_id);
    std::optional<int> get_worker(uint64_t label_id);

private:
    transport::RedisConnection& redis_;
    std::string catalog_key(uint64_t label_id) const;
};

} // namespace labios
