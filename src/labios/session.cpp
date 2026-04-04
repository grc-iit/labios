#include <labios/session.h>

#include <unistd.h>

namespace labios {

struct Session::Impl {
    Config cfg;
    transport::RedisConnection redis;
    transport::NatsConnection nats;
    ContentManager content;
    CatalogManager catalog;
    LabelManager labels;
    uint32_t app_id;

    explicit Impl(const Config& c)
        : cfg(c),
          redis(c.redis_host, c.redis_port),
          nats(c.nats_url),
          content(redis, c.label_min_size, c.cache_flush_interval_ms,
                  read_policy_from_string(c.cache_read_policy)),
          catalog(redis),
          labels(content, catalog, nats, c.label_max_size,
                 static_cast<uint32_t>(getpid()), c.reply_timeout_ms),
          app_id(static_cast<uint32_t>(getpid())) {}
};

Session::Session(const Config& cfg) : impl_(std::make_unique<Impl>(cfg)) {}

Session::~Session() {
    // Flush NATS to ensure all published labels reach the server before
    // the transport connections are destroyed. ContentManager's destructor
    // handles draining the small-I/O cache. Destruction order (reverse of
    // declaration) then tears down: labels, catalog, content, nats, redis.
    impl_->nats.flush();
}

LabelManager& Session::label_manager() { return impl_->labels; }
ContentManager& Session::content_manager() { return impl_->content; }
CatalogManager& Session::catalog_manager() { return impl_->catalog; }
transport::RedisConnection& Session::redis() { return impl_->redis; }
transport::NatsConnection& Session::nats() { return impl_->nats; }
const Config& Session::config() const { return impl_->cfg; }
uint32_t Session::app_id() const { return impl_->app_id; }

} // namespace labios
