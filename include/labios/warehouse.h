#pragma once

#include <labios/transport/redis.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace labios {

class Warehouse {
public:
    explicit Warehouse(transport::RedisConnection& redis);

    void stage(uint64_t label_id, std::span<const std::byte> data);
    std::vector<std::byte> retrieve(uint64_t label_id);
    void remove(uint64_t label_id);
    bool exists(uint64_t label_id);

    static std::string data_key(uint64_t label_id);

private:
    transport::RedisConnection& redis_;
};

} // namespace labios
