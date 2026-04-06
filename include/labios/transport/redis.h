#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios::transport {

class RedisConnection {
public:
    RedisConnection(std::string_view host, int port);
    ~RedisConnection();

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;
    RedisConnection(RedisConnection&&) = delete;
    RedisConnection& operator=(RedisConnection&&) = delete;

    void set(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key);

    void set_binary(std::string_view key, std::span<const std::byte> data);
    [[nodiscard]] std::vector<std::byte> get_binary(std::string_view key);
    void del(std::string_view key);
    void hset(std::string_view key, std::string_view field, std::string_view value);
    [[nodiscard]] std::optional<std::string> hget(std::string_view key, std::string_view field);

    // --- Pipelining ---
    void pipeline_begin();
    void pipeline_hset(std::string_view key, std::string_view field,
                       std::string_view value);
    void pipeline_set(std::string_view key, std::string_view value);
    void pipeline_set_binary(std::string_view key, std::span<const std::byte> data);
    void pipeline_del(std::string_view key);
    void pipeline_exec();

    // --- Sorted sets (for per-offset location tracking) ---
    void zadd(std::string_view key, double score, std::string_view member);
    struct ZRangeEntry { std::string member; double score; };
    [[nodiscard]] std::vector<ZRangeEntry> zrangebyscore(
        std::string_view key, double min, double max);

    /// Set a TTL on a key (Redis EXPIRE command).
    void expire(std::string_view key, uint32_t seconds);

    /// Scan keys matching a pattern. Returns all matching keys.
    /// Uses SCAN internally to avoid blocking on large keyspaces.
    [[nodiscard]] std::vector<std::string> scan_keys(std::string_view pattern);

    [[nodiscard]] bool connected() const;

    /// RAII guard that holds the connection mutex for the duration of a
    /// pipeline sequence. Calls pipeline_exec() on destruction.
    class PipelineGuard {
    public:
        explicit PipelineGuard(RedisConnection& conn);
        ~PipelineGuard();
        PipelineGuard(const PipelineGuard&) = delete;
        PipelineGuard& operator=(const PipelineGuard&) = delete;
    private:
        RedisConnection& conn_;
        std::unique_lock<std::mutex> lock_;
    };

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mu_;

    // Unlocked versions for use by PipelineGuard and internal callers
    // that already hold mu_.
    void set_locked(std::string_view key, std::string_view value);
    std::optional<std::string> get_locked(std::string_view key);
    void set_binary_locked(std::string_view key, std::span<const std::byte> data);
    std::vector<std::byte> get_binary_locked(std::string_view key);
    void del_locked(std::string_view key);
    void hset_locked(std::string_view key, std::string_view field, std::string_view value);
    std::optional<std::string> hget_locked(std::string_view key, std::string_view field);
    void expire_locked(std::string_view key, uint32_t seconds);
    std::vector<std::string> scan_keys_locked(std::string_view pattern);
    void pipeline_begin_locked();
    void pipeline_exec_locked();
};

} // namespace labios::transport
