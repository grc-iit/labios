#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios::transport {

/// A deferred reply handle returned by publish_request_async().
/// Call wait() to block until the reply arrives.
struct AsyncReply {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::byte> data;
    bool completed = false;

    /// Block until the reply arrives or timeout. Returns the reply data.
    std::vector<std::byte> wait(std::chrono::milliseconds timeout);
};

class NatsConnection {
public:
    explicit NatsConnection(std::string_view url);
    ~NatsConnection();

    NatsConnection(const NatsConnection&) = delete;
    NatsConnection& operator=(const NatsConnection&) = delete;
    NatsConnection(NatsConnection&&) noexcept;
    NatsConnection& operator=(NatsConnection&&) noexcept;

    void publish(std::string_view subject, std::span<const std::byte> data);

    /// Convenience overload for string payloads.
    void publish(std::string_view subject, std::string_view data);

    using MessageCallback = std::function<void(std::string_view subject,
                                               std::span<const std::byte> data,
                                               std::string_view reply_to)>;

    /// Subscribe to a subject. The callback is invoked on a cnats-managed thread.
    /// The subscription lives until this NatsConnection is destroyed.
    ///
    /// Note: calling subscribe() multiple times replaces the previous callback.
    /// All subscriptions share a single callback. This is sufficient for M0
    /// where each service subscribes once.
    void subscribe(std::string_view subject, MessageCallback callback);

    /// Flush the outbound buffer so published messages reach the server.
    void flush();

    struct Reply {
        std::vector<std::byte> data;
    };

    Reply request(std::string_view subject, std::span<const std::byte> data,
                  std::chrono::milliseconds timeout);

    /// Publish a message with a reply inbox, return immediately.
    /// The reply arrives asynchronously via the returned handle.
    std::shared_ptr<AsyncReply> publish_request_async(
        std::string_view subject, std::span<const std::byte> data);

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
