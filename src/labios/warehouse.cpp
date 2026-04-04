#include <labios/warehouse.h>

#include <string>

namespace labios {

Warehouse::Warehouse(transport::RedisConnection& redis) : redis_(redis) {}

std::string Warehouse::data_key(uint64_t label_id) {
    return "labios:data:" + std::to_string(label_id);
}

void Warehouse::stage(uint64_t label_id, std::span<const std::byte> data) {
    redis_.set_binary(data_key(label_id), data);
}

std::vector<std::byte> Warehouse::retrieve(uint64_t label_id) {
    return redis_.get_binary(data_key(label_id));
}

void Warehouse::remove(uint64_t label_id) {
    redis_.del(data_key(label_id));
}

bool Warehouse::exists(uint64_t label_id) {
    return redis_.get(data_key(label_id)).has_value();
}

} // namespace labios
