#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace labios::transport {

class RedisConnection {
public:
    RedisConnection(std::string_view host, int port);
    ~RedisConnection();

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;
    RedisConnection(RedisConnection&&) noexcept;
    RedisConnection& operator=(RedisConnection&&) noexcept;

    void set(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key);

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
