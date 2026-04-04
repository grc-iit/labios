#pragma once

#include <labios/transport/redis.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace labios {

struct FlushRegion {
    std::string filepath;
    uint64_t offset;
    std::vector<std::byte> data;
};

enum class ReadPolicy { ReadThrough, WriteOnly };

ReadPolicy read_policy_from_string(std::string_view s);

class ContentManager {
public:
    ContentManager(transport::RedisConnection& redis,
                   uint64_t min_label_size,
                   int flush_interval_ms,
                   ReadPolicy default_read_policy);
    ~ContentManager();

    ContentManager(const ContentManager&) = delete;
    ContentManager& operator=(const ContentManager&) = delete;

    // --- Warehouse (label-ID keyed staging) ---
    void stage(uint64_t label_id, std::span<const std::byte> data);
    std::vector<std::byte> retrieve(uint64_t label_id);
    void remove(uint64_t label_id);
    bool exists(uint64_t label_id);
    static std::string data_key(uint64_t label_id);

    // --- Small-I/O Cache ---
    std::vector<FlushRegion> cache_write(int fd, std::string_view filepath,
                                          uint64_t offset,
                                          std::span<const std::byte> data);
    std::optional<std::vector<std::byte>> cache_read(int fd, uint64_t offset,
                                                      uint64_t size);
    std::vector<FlushRegion> flush(int fd);
    std::vector<std::pair<int, std::vector<FlushRegion>>> flush_all();
    void evict(int fd);
    void set_read_policy(int fd, ReadPolicy policy);
    void start_flush_timer();

    using FlushCallback = std::function<void(int, std::vector<FlushRegion>)>;
    void set_flush_callback(FlushCallback cb);

private:
    transport::RedisConnection& redis_;
    uint64_t min_label_size_;
    int flush_interval_ms_;
    ReadPolicy default_read_policy_;

    struct FdCache {
        std::string filepath;
        ReadPolicy read_policy;
        std::map<uint64_t, std::vector<std::byte>> regions;
        uint64_t total_bytes = 0;
    };

    std::shared_mutex cache_mu_;
    std::unordered_map<int, FdCache> caches_;
    std::jthread flush_thread_;
    FlushCallback flush_callback_;

    std::vector<FlushRegion> flush_locked(FdCache& cache);
};

} // namespace labios
