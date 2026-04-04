#pragma once

#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/content_manager.h>
#include <labios/label_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <cstdint>
#include <memory>

namespace labios {

class Session {
public:
    explicit Session(const Config& cfg);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    LabelManager& label_manager();
    ContentManager& content_manager();
    CatalogManager& catalog_manager();
    transport::RedisConnection& redis();
    transport::NatsConnection& nats();
    const Config& config() const;
    uint32_t app_id() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios
