#include <labios/transport/redis.h>

#include <hiredis.h>

#include <stdexcept>
#include <utility>

namespace labios::transport {

struct RedisConnection::Impl {
    redisContext* ctx = nullptr;

    ~Impl() {
        if (ctx != nullptr) redisFree(ctx);
    }
};

RedisConnection::RedisConnection(std::string_view host, int port)
    : impl_(std::make_unique<Impl>()) {
    impl_->ctx = redisConnect(std::string(host).c_str(), port);
    if (impl_->ctx == nullptr) {
        throw std::runtime_error("redis: allocation failure");
    }
    if (impl_->ctx->err != 0) {
        std::string msg = "redis: " + std::string(impl_->ctx->errstr);
        redisFree(impl_->ctx);
        impl_->ctx = nullptr;
        throw std::runtime_error(msg);
    }
}

RedisConnection::~RedisConnection() = default;
RedisConnection::RedisConnection(RedisConnection&&) noexcept = default;
RedisConnection& RedisConnection::operator=(RedisConnection&&) noexcept = default;

void RedisConnection::set(std::string_view key, std::string_view value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "SET %b %b",
                     key.data(), key.size(),
                     value.data(), value.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis SET failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::optional<std::string> RedisConnection::get(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "GET %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis GET failed: " + std::string(impl_->ctx->errstr));
    }
    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result.emplace(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

bool RedisConnection::connected() const {
    return impl_ && impl_->ctx != nullptr && impl_->ctx->err == 0;
}

} // namespace labios::transport
