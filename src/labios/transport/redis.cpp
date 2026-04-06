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

// --- Locked (internal) implementations ---

void RedisConnection::set_locked(std::string_view key, std::string_view value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "SET %b %b",
                     key.data(), key.size(),
                     value.data(), value.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis SET failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::optional<std::string> RedisConnection::get_locked(std::string_view key) {
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

void RedisConnection::set_binary_locked(std::string_view key, std::span<const std::byte> data) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "SET %b %b",
                     key.data(), key.size(),
                     data.data(), data.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis SET (binary) failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::vector<std::byte> RedisConnection::get_binary_locked(std::string_view key) {
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

void RedisConnection::del_locked(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "DEL %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis DEL failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

void RedisConnection::hset_locked(std::string_view key, std::string_view field, std::string_view value) {
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

std::optional<std::string> RedisConnection::hget_locked(std::string_view key, std::string_view field) {
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

void RedisConnection::expire_locked(std::string_view key, uint32_t seconds) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "EXPIRE %b %u",
                     key.data(), key.size(), seconds));
    if (reply == nullptr) {
        throw std::runtime_error("redis EXPIRE failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::vector<std::string> RedisConnection::scan_keys_locked(std::string_view pattern) {
    std::vector<std::string> keys;
    unsigned long long cursor = 0;
    do {
        auto* reply = static_cast<redisReply*>(
            redisCommand(impl_->ctx, "SCAN %llu MATCH %b COUNT 100",
                         cursor, pattern.data(), pattern.size()));
        if (reply == nullptr) {
            throw std::runtime_error("redis SCAN failed: " + std::string(impl_->ctx->errstr));
        }
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
            cursor = std::stoull(reply->element[0]->str);
            auto* arr = reply->element[1];
            for (size_t i = 0; i < arr->elements; ++i) {
                keys.emplace_back(arr->element[i]->str, arr->element[i]->len);
            }
        } else {
            freeReplyObject(reply);
            break;
        }
        freeReplyObject(reply);
    } while (cursor != 0);
    return keys;
}

void RedisConnection::pipeline_begin_locked() {
    impl_->pipeline_count = 0;
}

void RedisConnection::pipeline_exec_locked() {
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

// --- Public thread-safe wrappers ---

void RedisConnection::set(std::string_view key, std::string_view value) {
    std::lock_guard lock(mu_);
    set_locked(key, value);
}

std::optional<std::string> RedisConnection::get(std::string_view key) {
    std::lock_guard lock(mu_);
    return get_locked(key);
}

void RedisConnection::set_binary(std::string_view key, std::span<const std::byte> data) {
    std::lock_guard lock(mu_);
    set_binary_locked(key, data);
}

std::vector<std::byte> RedisConnection::get_binary(std::string_view key) {
    std::lock_guard lock(mu_);
    return get_binary_locked(key);
}

void RedisConnection::del(std::string_view key) {
    std::lock_guard lock(mu_);
    del_locked(key);
}

void RedisConnection::hset(std::string_view key, std::string_view field, std::string_view value) {
    std::lock_guard lock(mu_);
    hset_locked(key, field, value);
}

std::optional<std::string> RedisConnection::hget(std::string_view key, std::string_view field) {
    std::lock_guard lock(mu_);
    return hget_locked(key, field);
}

void RedisConnection::expire(std::string_view key, uint32_t seconds) {
    std::lock_guard lock(mu_);
    expire_locked(key, seconds);
}

std::vector<std::string> RedisConnection::scan_keys(std::string_view pattern) {
    std::lock_guard lock(mu_);
    return scan_keys_locked(pattern);
}

bool RedisConnection::connected() const {
    return impl_ && impl_->ctx != nullptr && impl_->ctx->err == 0;
}

void RedisConnection::zadd(std::string_view key, double score, std::string_view member) {
    std::lock_guard lock(mu_);
    auto score_str = std::to_string(score);
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "ZADD %b %s %b",
                     key.data(), key.size(),
                     score_str.c_str(),
                     member.data(), member.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis ZADD failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::vector<RedisConnection::ZRangeEntry> RedisConnection::zrangebyscore(
    std::string_view key, double min, double max) {
    std::lock_guard lock(mu_);
    auto min_str = std::to_string(min);
    auto max_str = std::to_string(max);
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "ZRANGEBYSCORE %b %s %s WITHSCORES",
                     key.data(), key.size(),
                     min_str.c_str(), max_str.c_str()));
    if (reply == nullptr) {
        throw std::runtime_error("redis ZRANGEBYSCORE failed: " + std::string(impl_->ctx->errstr));
    }
    std::vector<ZRangeEntry> result;
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 2) {
        for (size_t i = 0; i + 1 < reply->elements; i += 2) {
            ZRangeEntry entry;
            entry.member = std::string(reply->element[i]->str, reply->element[i]->len);
            entry.score = std::stod(std::string(reply->element[i + 1]->str, reply->element[i + 1]->len));
            result.push_back(std::move(entry));
        }
    }
    freeReplyObject(reply);
    return result;
}

void RedisConnection::pipeline_begin() {
    mu_.lock();
    pipeline_begin_locked();
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
    try {
        pipeline_exec_locked();
    } catch (...) {
        mu_.unlock();
        throw;
    }
    mu_.unlock();
}

// --- PipelineGuard ---

RedisConnection::PipelineGuard::PipelineGuard(RedisConnection& conn)
    : conn_(conn), lock_(conn.mu_) {
    conn_.pipeline_begin_locked();
}

RedisConnection::PipelineGuard::~PipelineGuard() {
    try {
        conn_.pipeline_exec_locked();
    } catch (...) {
        // Pipeline failures in destructor cannot propagate.
    }
}

} // namespace labios::transport
