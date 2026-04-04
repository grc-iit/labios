#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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
    RedisConnection(RedisConnection&&) noexcept;
    RedisConnection& operator=(RedisConnection&&) noexcept;

    void set(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key);

    void set_binary(std::string_view key, std::span<const std::byte> data);
    [[nodiscard]] std::vector<std::byte> get_binary(std::string_view key);
    void del(std::string_view key);
    void hset(std::string_view key, std::string_view field, std::string_view value);
    [[nodiscard]] std::optional<std::string> hget(std::string_view key, std::string_view field);

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
