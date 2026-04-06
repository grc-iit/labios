#pragma once
#include <labios/backend/backend.h>
#include <labios/transport/redis.h>

namespace labios {

/// Key-value backend using Redis/DragonflyDB (kv:// scheme).
/// Key derived from label dest_uri path. Stores raw data as binary blob.
/// Metadata (intent, isolation, ttl) stored in a companion hash key.
class KVBackend {
public:
    explicit KVBackend(transport::RedisConnection& redis,
                       std::string prefix = "labios:kv:");

    BackendResult put(const LabelData& label, std::span<const std::byte> data);
    BackendDataResult get(const LabelData& label);
    BackendResult del(const LabelData& label);
    BackendQueryResult query(const LabelData& label);
    std::string_view scheme() const { return "kv"; }

private:
    transport::RedisConnection& redis_;
    std::string prefix_;
    std::string make_key(const LabelData& label) const;
};

static_assert(BackendStore<KVBackend>);

} // namespace labios
