#include <labios/client.h>
#include <labios/channel.h>
#include <labios/session.h>

#include <stdexcept>

namespace labios {

Client::Client(const Config& cfg)
    : session_(std::make_unique<Session>(cfg))
    , channels_(std::make_unique<ChannelRegistry>(session_->redis(), session_->nats())) {}
Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

// ---------------------------------------------------------------------------
// Synchronous convenience API
// ---------------------------------------------------------------------------

void Client::write(std::string_view filepath, std::span<const std::byte> data,
                   uint64_t offset) {
    auto& label_mgr = session_->label_manager();
    auto& catalog_mgr = session_->catalog_manager();

    auto pending = label_mgr.publish_write(filepath, offset, data);
    label_mgr.wait(pending);
    catalog_mgr.track_write(filepath, offset, data.size());
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                    uint64_t offset, uint64_t size) {
    auto& label_mgr = session_->label_manager();

    auto pending = label_mgr.publish_read(filepath, offset, size);
    return label_mgr.wait_read(pending);
}

// ---------------------------------------------------------------------------
// Asynchronous label API (paper Figure 4)
// ---------------------------------------------------------------------------

PendingIO Client::async_write(std::string_view filepath,
                                std::span<const std::byte> data,
                                uint64_t offset) {
    auto& label_mgr = session_->label_manager();
    return PendingIO{label_mgr.publish_write(filepath, offset, data)};
}

PendingIO Client::async_read(std::string_view filepath,
                               uint64_t offset, uint64_t size) {
    auto& label_mgr = session_->label_manager();
    return PendingIO{label_mgr.publish_read(filepath, offset, size)};
}

void Client::wait(PendingIO& status) {
    session_->label_manager().wait(status.pending);
}

std::vector<std::byte> Client::wait_read(PendingIO& status) {
    return session_->label_manager().wait_read(status.pending);
}

// ---------------------------------------------------------------------------
// Label-level API (advanced)
// ---------------------------------------------------------------------------

LabelData Client::create_label(const LabelParams& params) {
    LabelData label{};
    label.id = generate_label_id(session_->app_id());
    label.type = params.type;
    label.source = params.source;
    label.destination = params.destination;
    label.operation = params.operation;
    label.flags = params.flags;
    label.priority = params.priority;
    label.dependencies = params.dependencies;
    label.intent = params.intent;
    label.ttl_seconds = params.ttl_seconds;
    label.isolation = params.isolation;
    label.app_id = session_->app_id();
    return label;
}

PendingIO Client::publish(const LabelData& label,
                            std::span<const std::byte> data) {
    auto& content = session_->content_manager();
    auto& nats = session_->nats();

    if (!data.empty()) {
        content.stage(label.id, data);
    }

    LabelData mutable_label = label;
    auto serialized = serialize_label(mutable_label);
    auto async = nats.publish_request_async("labios.labels", serialized);
    nats.flush();

    PendingLabel pl;
    pl.label_id = label.id;
    pl.async_reply = std::move(async);
    std::vector<PendingLabel> vec;
    vec.push_back(std::move(pl));
    return PendingIO{std::move(vec)};
}

// ---------------------------------------------------------------------------
// Channel API (streaming coordination)
// ---------------------------------------------------------------------------

Channel* Client::create_channel(std::string_view name, uint32_t ttl_seconds) {
    return channels_->create(name, ttl_seconds);
}

Channel* Client::get_channel(std::string_view name) {
    return channels_->get(name);
}

uint64_t Client::publish_to_channel(std::string_view channel_name,
                                    std::span<const std::byte> data,
                                    uint64_t label_id) {
    auto* ch = channels_->get(channel_name);
    if (ch == nullptr) return 0;
    return ch->publish(data, label_id);
}

int Client::subscribe_to_channel(std::string_view channel_name,
                                 ChannelCallback cb) {
    auto* ch = channels_->get(channel_name);
    if (ch == nullptr) return -1;
    return ch->subscribe(std::move(cb));
}

void Client::unsubscribe_from_channel(std::string_view channel_name,
                                      int sub_id) {
    auto* ch = channels_->get(channel_name);
    if (ch != nullptr) {
        ch->unsubscribe(sub_id);
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Session& Client::session() { return *session_; }
const Config& Client::config() const { return session_->config(); }
uint32_t Client::app_id() const { return session_->app_id(); }

Client connect(const Config& cfg) { return Client(cfg); }

} // namespace labios
