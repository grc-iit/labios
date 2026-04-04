#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace labios::transport {

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
                                               std::span<const std::byte> data)>;

    /// Subscribe to a subject. The callback is invoked on a cnats-managed thread.
    /// The subscription lives until this NatsConnection is destroyed.
    ///
    /// Note: calling subscribe() multiple times replaces the previous callback.
    /// All subscriptions share a single callback. This is sufficient for M0
    /// where each service subscribes once.
    void subscribe(std::string_view subject, MessageCallback callback);

    /// Flush the outbound buffer so published messages reach the server.
    void flush();

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
