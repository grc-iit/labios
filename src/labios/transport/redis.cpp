#include <labios/transport/redis.h>

#include <hiredis.h>

#include <stdexcept>
#include <utility>

namespace labios::transport {

struct RedisConnection::Impl {
    redisContext* ctx = nullptr;
    int pipeline_count = 0;

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

void RedisConnection::set_binary(std::string_view key, std::span<const std::byte> data) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "SET %b %b",
                     key.data(), key.size(),
                     data.data(), data.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis SET (binary) failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::vector<std::byte> RedisConnection::get_binary(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "GET %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis GET (binary) failed: " + std::string(impl_->ctx->errstr));
    }
    std::vector<std::byte> result;
    if (reply->type == REDIS_REPLY_STRING) {
        auto* raw = reinterpret_cast<const std::byte*>(reply->str);
        result.assign(raw, raw + reply->len);
    }
    freeReplyObject(reply);
    return result;
}

void RedisConnection::del(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "DEL %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis DEL failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

void RedisConnection::hset(std::string_view key, std::string_view field, std::string_view value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "HSET %b %b %b",
                     key.data(), key.size(),
                     field.data(), field.size(),
                     value.data(), value.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis HSET failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::optional<std::string> RedisConnection::hget(std::string_view key, std::string_view field) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "HGET %b %b",
                     key.data(), key.size(),
                     field.data(), field.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis HGET failed: " + std::string(impl_->ctx->errstr));
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

void RedisConnection::pipeline_begin() {
    impl_->pipeline_count = 0;
}

void RedisConnection::pipeline_hset(std::string_view key,
                                     std::string_view field,
                                     std::string_view value) {
    int rc = redisAppendCommand(impl_->ctx, "HSET %b %b %b",
                                key.data(), key.size(),
                                field.data(), field.size(),
                                value.data(), value.size());
    if (rc != REDIS_OK) {
        throw std::runtime_error("redis pipeline HSET failed");
    }
    ++impl_->pipeline_count;
}

void RedisConnection::pipeline_set(std::string_view key,
                                    std::string_view value) {
    int rc = redisAppendCommand(impl_->ctx, "SET %b %b",
                                key.data(), key.size(),
                                value.data(), value.size());
    if (rc != REDIS_OK) {
        throw std::runtime_error("redis pipeline SET failed");
    }
    ++impl_->pipeline_count;
}

void RedisConnection::pipeline_set_binary(std::string_view key,
                                           std::span<const std::byte> data) {
    int rc = redisAppendCommand(impl_->ctx, "SET %b %b",
                                key.data(), key.size(),
                                data.data(), data.size());
    if (rc != REDIS_OK) {
        throw std::runtime_error("redis pipeline SET (binary) failed");
    }
    ++impl_->pipeline_count;
}

void RedisConnection::pipeline_del(std::string_view key) {
    int rc = redisAppendCommand(impl_->ctx, "DEL %b",
                                key.data(), key.size());
    if (rc != REDIS_OK) {
        throw std::runtime_error("redis pipeline DEL failed");
    }
    ++impl_->pipeline_count;
}

void RedisConnection::pipeline_exec() {
    for (int i = 0; i < impl_->pipeline_count; ++i) {
        redisReply* reply = nullptr;
        int rc = redisGetReply(impl_->ctx, reinterpret_cast<void**>(&reply));
        if (rc != REDIS_OK || reply == nullptr) {
            for (int j = i + 1; j < impl_->pipeline_count; ++j) {
                redisReply* drain = nullptr;
                redisGetReply(impl_->ctx, reinterpret_cast<void**>(&drain));
                if (drain) freeReplyObject(drain);
            }
            impl_->pipeline_count = 0;
            throw std::runtime_error("redis pipeline exec failed at command "
                                     + std::to_string(i));
        }
        freeReplyObject(reply);
    }
    impl_->pipeline_count = 0;
}

} // namespace labios::transport
