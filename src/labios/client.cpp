#include <labios/client.h>
#include <labios/channel.h>
#include <labios/session.h>
#include <labios/workspace.h>

#include <stdexcept>

namespace labios {

Client::Client(const Config& cfg)
    : session_(std::make_unique<Session>(cfg))
    , channels_(std::make_unique<ChannelRegistry>(session_->redis(), session_->nats()))
    , workspaces_(std::make_unique<WorkspaceRegistry>(session_->redis())) {}
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

    // Spec fields (Wave 10)
    label.version = params.version;
    label.durability = params.durability;
    label.continuation = params.continuation;
    label.source_uri = params.source_uri;
    label.dest_uri = params.dest_uri;
    label.pipeline = params.pipeline;
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
// Workspace API (persistent shared state)
// ---------------------------------------------------------------------------

Workspace* Client::create_workspace(std::string_view name, uint32_t ttl_seconds) {
    return workspaces_->create(name, session_->app_id(), ttl_seconds);
}

Workspace* Client::get_workspace(std::string_view name) {
    return workspaces_->get(name);
}

uint64_t Client::workspace_put(std::string_view workspace, std::string_view key,
                                std::span<const std::byte> data) {
    auto* ws = workspaces_->get(workspace);
    if (ws == nullptr) return 0;
    return ws->put(key, data, session_->app_id());
}

std::optional<std::vector<std::byte>> Client::workspace_get(
    std::string_view workspace, std::string_view key) {
    auto* ws = workspaces_->get(workspace);
    if (ws == nullptr) return std::nullopt;
    return ws->get(key, session_->app_id());
}

bool Client::workspace_del(std::string_view workspace, std::string_view key) {
    auto* ws = workspaces_->get(workspace);
    if (ws == nullptr) return false;
    return ws->del(key, session_->app_id());
}

void Client::workspace_grant(std::string_view workspace, uint32_t app_id) {
    auto* ws = workspaces_->get(workspace);
    if (ws != nullptr) {
        ws->grant_access(app_id);
    }
}

// ---------------------------------------------------------------------------
// Observability API
// ---------------------------------------------------------------------------

std::string Client::observe(std::string_view query) {
    LabelParams params{};
    params.type = LabelType::Observe;
    params.source_uri = std::string("observe://") + std::string(query);
    auto label = create_label(params);

    auto pending = publish(label);
    wait(pending);

    // Retrieve result from warehouse using the label ID as key.
    auto& content = session_->content_manager();
    auto result = content.retrieve(label.id);
    return {reinterpret_cast<const char*>(result.data()), result.size()};
}

// ---------------------------------------------------------------------------
// URI-based I/O
// ---------------------------------------------------------------------------

void Client::write_to(std::string_view dest_uri,
                      std::span<const std::byte> data) {
    auto pending = async_write_to(dest_uri, data);
    wait(pending);
}

PendingIO Client::async_write_to(std::string_view dest_uri,
                                  std::span<const std::byte> data) {
    LabelParams params{};
    params.type = LabelType::Write;
    params.dest_uri = std::string(dest_uri);
    auto label = create_label(params);
    label.data_size = data.size();
    return publish(label, data);
}

std::vector<std::byte> Client::read_from(std::string_view source_uri,
                                          uint64_t size) {
    auto pending = async_read_from(source_uri, size);
    return wait_read(pending);
}

PendingIO Client::async_read_from(std::string_view source_uri, uint64_t size) {
    LabelParams params{};
    params.type = LabelType::Read;
    params.source_uri = std::string(source_uri);
    auto label = create_label(params);
    label.data_size = size;
    return publish(label);
}

// ---------------------------------------------------------------------------
// Intent-driven convenience API
// ---------------------------------------------------------------------------

PendingIO Client::write_with_intent(std::string_view filepath,
                                     std::span<const std::byte> data,
                                     Intent intent, uint8_t priority) {
    LabelParams params{};
    params.type = LabelType::Write;
    params.destination = file_path(filepath);
    params.intent = intent;
    params.priority = priority;
    auto label = create_label(params);
    label.data_size = data.size();
    return publish(label, data);
}

PendingIO Client::execute_pipeline(std::string_view source_uri,
                                    std::string_view dest_uri,
                                    const sds::Pipeline& pipeline,
                                    Intent intent) {
    LabelParams params{};
    params.type = LabelType::Write;
    params.source_uri = std::string(source_uri);
    params.dest_uri = std::string(dest_uri);
    params.pipeline = pipeline;
    params.intent = intent;
    auto label = create_label(params);
    return publish(label);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

std::string Client::get_config() {
    return observe("config/current");
}

bool Client::set_config(std::string_view key, std::string_view value) {
    return session_->mutable_config().set(std::string(key), std::string(value));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

Session& Client::session() { return *session_; }
const Config& Client::config() const { return session_->config(); }
uint32_t Client::app_id() const { return session_->app_id(); }

Client connect(const Config& cfg) { return Client(cfg); }

} // namespace labios
