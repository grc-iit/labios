#pragma once

#include <labios/transparent_string_hash.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace labios {

struct ChannelMessage {
    uint64_t sequence;          // Monotonic per-channel ordering
    uint64_t label_id;          // Label that produced this message (0 if raw)
    std::vector<std::byte> data;
};

using ChannelCallback = std::function<void(const ChannelMessage&)>;

/// A named data channel for streaming between agents.
/// Data is staged in the warehouse (DragonflyDB). Notifications go via NATS.
/// Auto-destroys when reference count reaches zero during drain.
class Channel {
public:
    Channel(std::string name,
            transport::RedisConnection& redis,
            transport::NatsConnection& nats,
            uint32_t ttl_seconds = 0,
            uint64_t max_pending = 10000);

    const std::string& name() const { return name_; }

    /// Publish data to the channel. Returns the sequence number assigned.
    /// Returns 0 if the channel is draining or destroyed.
    uint64_t publish(std::span<const std::byte> data, uint64_t label_id = 0);

    /// Subscribe to the channel. Callback fires for each new message.
    /// Returns a subscription ID for unsubscribe.
    int subscribe(ChannelCallback cb);

    /// Unsubscribe. If this was the last subscriber and the channel is
    /// draining, the channel auto-destroys.
    void unsubscribe(int sub_id);

    /// Number of active subscribers.
    int subscriber_count() const;

    /// Drain: stop accepting new publishes, wait for subscribers to
    /// consume remaining messages, then destroy.
    void drain();

    /// Destroy the channel immediately, cleaning up warehouse keys.
    void destroy();

    bool is_destroyed() const { return destroyed_.load(); }

private:
    std::string name_;
    transport::RedisConnection& redis_;
    transport::NatsConnection& nats_;
    uint32_t ttl_seconds_;
    uint64_t max_pending_;

    mutable std::shared_mutex mu_;
    std::atomic<uint64_t> next_seq_{1};
    std::unordered_map<int, ChannelCallback> subscribers_;
    int next_sub_id_ = 1;
    std::atomic<bool> draining_{false};
    std::atomic<bool> destroyed_{false};

    std::string nats_subject() const;               // "labios.channel.<name>"
    std::string warehouse_key(uint64_t seq) const;   // "labios:channel:<name>:<seq>"
    std::string warehouse_pattern() const;            // "labios:channel:<name>:*"
    void cleanup_warehouse();
};

/// Registry of active channels. Thread-safe.
class ChannelRegistry {
public:
    ChannelRegistry(transport::RedisConnection& redis,
                    transport::NatsConnection& nats);

    /// Create a new channel. Returns nullptr if name already exists.
    Channel* create(std::string_view name, uint32_t ttl_seconds = 0);

    /// Get an existing channel by name. Returns nullptr if not found.
    Channel* get(std::string_view name);

    /// Remove a destroyed channel from the registry.
    void remove(std::string_view name);

    /// List all active channel names.
    std::vector<std::string> list() const;

private:
    transport::RedisConnection& redis_;
    transport::NatsConnection& nats_;
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<Channel>,
                       TransparentStringHash, std::equal_to<>> channels_;
};

} // namespace labios
