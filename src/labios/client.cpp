#include <labios/client.h>

#include <labios/catalog_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/content_manager.h>

#include <chrono>
#include <mutex>
#include <stdexcept>

#include <unistd.h>

namespace labios {

// ---------------------------------------------------------------------------
// Status::Impl
// ---------------------------------------------------------------------------

struct Status::Impl {
    uint64_t label_id = 0;
    std::vector<std::byte> reply_data;
    bool completed = false;
    CompletionData completion;
    std::mutex mu;
};

void Status::wait() {
    std::lock_guard lock(impl_->mu);
    if (impl_->completed) {
        return;
    }
    impl_->completion =
        deserialize_completion(std::span<const std::byte>(impl_->reply_data));
    impl_->completed = true;
}

bool Status::ready() const {
    // In M1a the NATS request is synchronous, so the reply data is always
    // available by the time the caller receives the Status object.
    return !impl_->reply_data.empty();
}

CompletionStatus Status::result() const {
    const_cast<Status*>(this)->wait();
    std::lock_guard lock(impl_->mu);
    return impl_->completion.status;
}

std::string Status::error() const {
    const_cast<Status*>(this)->wait();
    std::lock_guard lock(impl_->mu);
    return impl_->completion.error;
}

std::string Status::data_key() const {
    const_cast<Status*>(this)->wait();
    std::lock_guard lock(impl_->mu);
    return impl_->completion.data_key;
}

uint64_t Status::label_id() const {
    return impl_->label_id;
}

// ---------------------------------------------------------------------------
// Client::Impl
// ---------------------------------------------------------------------------

struct Client::Impl {
    Config cfg;
    transport::RedisConnection redis;
    transport::NatsConnection nats;
    ContentManager content_manager;
    CatalogManager catalog;
    uint32_t app_id;

    explicit Impl(const Config& c)
        : cfg(c),
          redis(c.redis_host, c.redis_port),
          nats(c.nats_url),
          content_manager(redis, c.label_min_size, c.cache_flush_interval_ms,
                          read_policy_from_string(c.cache_read_policy)),
          catalog(redis),
          app_id(static_cast<uint32_t>(getpid())) {}
};

Client::Client(const Config& cfg) : impl_(std::make_unique<Impl>(cfg)) {}

Client::~Client() = default;

Label Client::create_label(const LabelParams& params) {
    Label label;
    label.data_.id = generate_label_id(impl_->app_id);
    label.data_.type = params.type;
    label.data_.source = params.source;
    label.data_.destination = params.destination;
    label.data_.operation = params.operation;
    label.data_.flags = params.flags;
    label.data_.priority = params.priority;
    label.data_.app_id = impl_->app_id;
    label.data_.intent = params.intent;
    label.data_.ttl_seconds = params.ttl_seconds;
    label.data_.isolation = params.isolation;
    label.serialized_ = serialize_label(label.data_);
    return label;
}

Status Client::publish(const Label& label, std::span<const std::byte> data) {
    auto id = label.id();

    if (label.type() == LabelType::Write && !data.empty()) {
        // Stage data in the warehouse before dispatching.
        impl_->content_manager.stage(id, data);
    }

    // Create catalog entry with Queued status.
    impl_->catalog.create(id, impl_->app_id, label.type());

    // If this is a write, re-serialize with the actual data_size so the
    // dispatcher and worker know how large the payload is.
    std::span<const std::byte> payload = label.serialized();
    std::vector<std::byte> reserialized;

    if (label.type() == LabelType::Write && !data.empty()) {
        LabelData patched = label.data();
        patched.data_size = data.size();
        reserialized = serialize_label(patched);
        payload = reserialized;
    }

    // Synchronous NATS request with 30s timeout.
    auto reply = impl_->nats.request(
        "labios.labels", payload, std::chrono::milliseconds(30000));

    // Build the Status from the reply.
    auto status_impl = std::make_shared<Status::Impl>();
    status_impl->label_id = id;
    status_impl->reply_data = std::move(reply.data);

    Status status;
    status.impl_ = std::move(status_impl);
    return status;
}

void Client::write(std::string_view filepath, std::span<const std::byte> data,
                   uint64_t offset) {
    LabelParams params{};
    params.type = LabelType::Write;
    params.source = memory_ptr(data.data(), data.size());
    params.destination = file_path(filepath, offset, data.size());
    auto label = create_label(params);

    auto status = publish(label, data);
    status.wait();

    if (status.result() == CompletionStatus::Error) {
        throw std::runtime_error("write failed: " + status.error());
    }
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                    uint64_t offset, uint64_t size) {
    // For reads, set data_size so the worker knows how many bytes to read.
    // Build the label directly to inject data_size before serialization.
    Label label;
    label.data_.id = generate_label_id(impl_->app_id);
    label.data_.type = LabelType::Read;
    label.data_.source = file_path(filepath, offset, size);
    label.data_.app_id = impl_->app_id;
    label.data_.data_size = size;
    label.serialized_ = serialize_label(label.data_);

    auto status = publish(label, {});
    status.wait();

    if (status.result() == CompletionStatus::Error) {
        throw std::runtime_error("read failed: " + status.error());
    }

    // Retrieve data from warehouse using the completion's data_key.
    auto key = status.data_key();
    auto result = impl_->redis.get_binary(key);
    impl_->redis.del(key);
    return result;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

Client connect(const Config& cfg) {
    return Client(cfg);
}

} // namespace labios
