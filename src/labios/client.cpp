#include <labios/client.h>
#include <labios/session.h>

#include "operation_context.h"

#include <stdexcept>

namespace labios {

namespace {

[[noreturn]] void throw_invalid_handle(std::string_view kind) {
    throw ClientError(ClientErrorCode::InvalidArgument, "INVALID_HANDLE",
                      std::string(kind) + " handle is empty");
}

void throw_on_unsuccessful_wait(const WaitResult& result) {
    if (result.state == CompletionState::Timeout) {
        throw CompletionError(
            CompletionState::Timeout, 0,
            "timed out waiting for labels; operations remain active");
    }
    for (const auto& item : result.results) {
        if (item.state == CompletionState::Failed ||
            item.state == CompletionState::Cancelled ||
            item.state == CompletionState::Unknown) {
            throw CompletionError(
                item.state, item.label_id,
                "label " + std::to_string(item.label_id) + " did not complete: " +
                    item.category + ": " + item.error);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Operation
// ---------------------------------------------------------------------------

Operation::Operation(std::shared_ptr<detail::OperationContext> context,
                     std::vector<uint64_t> label_ids, OperationKind kind)
    : context_(std::move(context)), label_ids_(std::move(label_ids)), kind_(kind) {}

bool Operation::valid() const noexcept {
    return context_ != nullptr;
}

uint64_t Operation::label_id(size_t index) const {
    if (index >= label_ids_.size()) {
        throw ClientError(ClientErrorCode::InvalidArgument, "INVALID_ARGUMENT",
                          "label index is outside the operation");
    }
    return label_ids_[index];
}

CompletionResult Operation::test(size_t index) const {
    if (!context_) throw_invalid_handle("operation");
    if (label_ids_.empty() && index == 0) {
        return {0, CompletionState::Complete, {}, {}, {}};
    }
    return context_->test(label_id(index));
}

WaitResult Operation::wait_for(std::chrono::milliseconds timeout) const {
    return wait_all(timeout);
}

WaitResult Operation::wait_any(std::chrono::milliseconds timeout) const {
    if (!context_) throw_invalid_handle("operation");
    return context_->wait_any(label_ids_, timeout);
}

WaitResult Operation::wait_all(std::chrono::milliseconds timeout) const {
    if (!context_) throw_invalid_handle("operation");
    return context_->wait_all(label_ids_, timeout);
}

WaitResult Operation::wait() const {
    if (!context_) throw_invalid_handle("operation");
    auto result = context_->wait_all(label_ids_, context_->default_timeout());
    throw_on_unsuccessful_wait(result);
    return result;
}

std::vector<CancellationResult> Operation::cancel() const {
    if (!context_) throw_invalid_handle("operation");
    std::vector<CancellationResult> results;
    results.reserve(label_ids_.size());
    for (const auto id : label_ids_) results.push_back(context_->cancel(id));
    return results;
}

std::vector<std::byte> Operation::read(std::chrono::milliseconds timeout) const {
    if (!context_) throw_invalid_handle("operation");
    if (kind_ != OperationKind::Read) {
        throw ClientError(ClientErrorCode::InvalidArgument, "INVALID_OPERATION_KIND",
                          "read() requires a read operation handle");
    }
    return context_->read(label_ids_, timeout);
}

// ---------------------------------------------------------------------------
// Owning transient-resource handles
// ---------------------------------------------------------------------------

const std::string& ChannelHandle::name() const {
    if (!channel_) throw_invalid_handle("channel");
    return channel_->name();
}
uint64_t ChannelHandle::publish(std::span<const std::byte> data,
                                uint64_t label_id) const {
    if (!channel_) throw_invalid_handle("channel");
    return channel_->publish(data, label_id);
}
int ChannelHandle::subscribe(ChannelCallback callback) const {
    if (!channel_) throw_invalid_handle("channel");
    return channel_->subscribe(std::move(callback));
}
void ChannelHandle::unsubscribe(int subscription_id) const {
    if (!channel_) throw_invalid_handle("channel");
    channel_->unsubscribe(subscription_id);
}
int ChannelHandle::subscriber_count() const {
    if (!channel_) throw_invalid_handle("channel");
    return channel_->subscriber_count();
}
void ChannelHandle::drain() const {
    if (!channel_) throw_invalid_handle("channel");
    channel_->drain();
}
void ChannelHandle::destroy() const {
    if (!channel_) throw_invalid_handle("channel");
    channel_->destroy();
}
bool ChannelHandle::is_destroyed() const {
    return !channel_ || channel_->is_destroyed();
}

const std::string& WorkspaceHandle::name() const {
    if (!workspace_) throw_invalid_handle("workspace");
    return workspace_->name();
}
uint32_t WorkspaceHandle::owner() const {
    if (!workspace_) throw_invalid_handle("workspace");
    return workspace_->owner();
}
uint64_t WorkspaceHandle::put(std::string_view key,
                              std::span<const std::byte> data) const {
    if (!workspace_) throw_invalid_handle("workspace");
    return workspace_->put(key, data, app_id_);
}
std::optional<std::vector<std::byte>> WorkspaceHandle::get(
    std::string_view key) const {
    if (!workspace_) throw_invalid_handle("workspace");
    return workspace_->get(key, app_id_);
}
std::optional<std::vector<std::byte>> WorkspaceHandle::get_version(
    std::string_view key, uint64_t version) const {
    if (!workspace_) throw_invalid_handle("workspace");
    return workspace_->get_version(key, version, app_id_);
}
bool WorkspaceHandle::erase(std::string_view key) const {
    if (!workspace_) throw_invalid_handle("workspace");
    return workspace_->del(key, app_id_);
}
std::vector<WorkspaceEntry> WorkspaceHandle::list(std::string_view prefix) const {
    if (!workspace_) throw_invalid_handle("workspace");
    return prefix.empty() ? workspace_->list(app_id_)
                          : workspace_->list(prefix, app_id_);
}
void WorkspaceHandle::grant(uint32_t app_id) const {
    if (!workspace_) throw_invalid_handle("workspace");
    workspace_->grant_access(app_id);
}
void WorkspaceHandle::revoke(uint32_t app_id) const {
    if (!workspace_) throw_invalid_handle("workspace");
    workspace_->revoke_access(app_id);
}
void WorkspaceHandle::destroy() const {
    if (!workspace_) throw_invalid_handle("workspace");
    workspace_->destroy();
}
bool WorkspaceHandle::is_destroyed() const {
    return !workspace_ || workspace_->is_destroyed();
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(const Config& cfg)
    : session_(std::make_shared<Session>(cfg)),
      operation_context_(
          std::make_shared<detail::SessionOperationContext>(session_)),
      channels_(std::make_shared<ChannelRegistry>(session_->redis(), session_->nats())),
      workspaces_(std::make_shared<WorkspaceRegistry>(session_->redis())) {}
Client::~Client() = default;
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

Operation Client::operation(std::span<const uint64_t> label_ids,
                            OperationKind kind) const {
    if (!operation_context_) throw_invalid_handle("client");
    return detail::OperationFactory::create(
        operation_context_, {label_ids.begin(), label_ids.end()}, kind);
}

void Client::write(std::string_view filepath, std::span<const std::byte> data,
                   uint64_t offset) {
    auto pending = async_write(filepath, data, offset);
    wait(pending);
    session_->catalog_manager().track_write(filepath, offset, data.size());
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                    uint64_t offset, uint64_t size) {
    return async_read(filepath, offset, size).read(
        std::chrono::milliseconds(session_->config().reply_timeout_ms));
}

Operation Client::async_write(std::string_view filepath,
                              std::span<const std::byte> data,
                              uint64_t offset) {
    return operation(session_->label_manager().publish_write(filepath, offset, data));
}

Operation Client::async_read(std::string_view filepath,
                             uint64_t offset, uint64_t size) {
    return operation(session_->label_manager().publish_read(filepath, offset, size),
                     OperationKind::Read);
}

CompletionResult Client::test(const Operation& pending) const {
    return pending.test();
}
void Client::wait(const Operation& pending) const {
    (void)pending.wait();
}
WaitResult Client::wait_for(const Operation& pending,
                            std::chrono::milliseconds timeout) const {
    return pending.wait_for(timeout);
}
WaitResult Client::wait_any(std::span<const uint64_t> label_ids,
                            std::chrono::milliseconds timeout) const {
    return operation_context_->wait_any(label_ids, timeout);
}
WaitResult Client::wait_all(std::span<const uint64_t> label_ids,
                            std::chrono::milliseconds timeout) const {
    return operation_context_->wait_all(label_ids, timeout);
}
bool Client::cancel(uint64_t label_id) const {
    const auto result = cancel_label(label_id);
    return result.state == CancellationState::Cancelled;
}
CancellationResult Client::cancel_label(uint64_t label_id) const {
    return operation_context_->cancel(label_id);
}
std::vector<std::byte> Client::wait_read(const Operation& pending) const {
    return pending.read(std::chrono::milliseconds(session_->config().reply_timeout_ms));
}

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
    label.version = params.version;
    label.durability = params.durability;
    label.continuation = params.continuation;
    label.source_uri = params.source_uri;
    label.dest_uri = params.dest_uri;
    label.pipeline = params.pipeline;
    label.declared_dependencies = params.declared_dependencies;
    label.source_resource = params.source_resource;
    label.destination_resource = params.destination_resource;
    label.has_source_resource = params.has_source_resource;
    label.has_destination_resource = params.has_destination_resource;
    label.input_binding = params.input_binding;
    label.has_input_binding = params.has_input_binding;
    normalize_label_resources(label);
    mark_label_created(label);
    return label;
}

Operation Client::publish(const LabelData& label,
                          std::span<const std::byte> data) {
    LabelData submitted = label;
    if (!data.empty()) {
        submitted.input_binding.provenance = submitted.has_source_resource
            ? BindingProvenance::MaterializedSource : BindingProvenance::DirectProducer;
        submitted.input_binding.content_id = std::to_string(label.id);
        submitted.input_binding.logical_length = data.size();
        submitted.has_input_binding = true;
        if (submitted.data_size == 0) submitted.data_size = data.size();
    }
    normalize_label_resources(submitted);
    if (submitted.created_us == 0) mark_label_created(submitted);
    validate_label_admission(submitted);

    auto [reply_to, reply] = session_->nats().create_reply_inbox();
    submitted.reply_to = std::move(reply_to);
    const auto serialized = serialize_label(submitted);
    if (!data.empty()) session_->content_manager().stage(label.id, data);
    session_->catalog_manager().create(submitted);
    session_->nats().publish_durable("labios.labels", serialized);
    session_->nats().flush();
    session_->label_manager().register_reply(label.id, std::move(reply));
    return operation(std::span<const uint64_t>(&submitted.id, 1),
                     submitted.type == LabelType::Read
                         ? OperationKind::Read : OperationKind::Generic);
}

ChannelHandle Client::create_channel(std::string_view name, uint32_t ttl_seconds) {
    return {session_, channels_->create(name, ttl_seconds)};
}
ChannelHandle Client::get_channel(std::string_view name) {
    return {session_, channels_->get(name)};
}
uint64_t Client::publish_to_channel(std::string_view channel_name,
                                    std::span<const std::byte> data,
                                    uint64_t label_id) {
    return get_channel(channel_name).publish(data, label_id);
}
int Client::subscribe_to_channel(std::string_view channel_name,
                                 ChannelCallback callback) {
    return get_channel(channel_name).subscribe(std::move(callback));
}
void Client::unsubscribe_from_channel(std::string_view channel_name,
                                      int subscription_id) {
    get_channel(channel_name).unsubscribe(subscription_id);
}

WorkspaceHandle Client::create_workspace(std::string_view name,
                                         uint32_t ttl_seconds) {
    return {session_, workspaces_->create(name, session_->app_id(), ttl_seconds),
            session_->app_id()};
}
WorkspaceHandle Client::get_workspace(std::string_view name) {
    return {session_, workspaces_->get(name), session_->app_id()};
}
uint64_t Client::workspace_put(std::string_view workspace, std::string_view key,
                               std::span<const std::byte> data) {
    return get_workspace(workspace).put(key, data);
}
std::optional<std::vector<std::byte>> Client::workspace_get(
    std::string_view workspace, std::string_view key) {
    return get_workspace(workspace).get(key);
}
bool Client::workspace_del(std::string_view workspace, std::string_view key) {
    return get_workspace(workspace).erase(key);
}
void Client::workspace_grant(std::string_view workspace, uint32_t app_id) {
    get_workspace(workspace).grant(app_id);
}

std::string Client::observe(std::string_view query) {
    LabelParams params{};
    params.type = LabelType::Observe;
    params.source_uri = std::string("observe://") + std::string(query);
    const auto label = create_label(params);
    auto pending = publish(label);
    const auto completed = pending.wait();
    const auto data_key = completed.results.empty() ||
                                  completed.results.front().data_key.empty()
        ? ContentManager::data_key(label.id)
        : completed.results.front().data_key;
    const auto result = session_->content_manager().retrieve_key(data_key);
    return {reinterpret_cast<const char*>(result.data()), result.size()};
}

void Client::write_to(std::string_view dest_uri,
                      std::span<const std::byte> data) {
    wait(async_write_to(dest_uri, data));
}
Operation Client::async_write_to(std::string_view dest_uri,
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
    return async_read_from(source_uri, size).read(
        std::chrono::milliseconds(session_->config().reply_timeout_ms));
}
Operation Client::async_read_from(std::string_view source_uri, uint64_t size) {
    LabelParams params{};
    params.type = LabelType::Read;
    params.source_uri = std::string(source_uri);
    auto label = create_label(params);
    label.data_size = size;
    return publish(label);
}

Operation Client::write_with_intent(std::string_view filepath,
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
Operation Client::execute_pipeline(std::string_view source_uri,
                                   std::string_view dest_uri,
                                   const sds::Pipeline& pipeline,
                                   Intent intent) {
    LabelParams params{};
    params.type = LabelType::Write;
    params.source_uri = std::string(source_uri);
    params.dest_uri = std::string(dest_uri);
    params.pipeline = pipeline;
    params.intent = intent;
    return publish(create_label(params));
}

LabelData Client::inspect_label(uint64_t label_id) {
    const auto label = session_->catalog_manager().get_snapshot(label_id);
    if (!label) {
        throw ClientError(ClientErrorCode::LookupFailed, "LOOKUP_FAILED",
                          "label is unknown or its catalog record expired");
    }
    return *label;
}

std::string Client::get_config() { return observe("config/current"); }
bool Client::set_config(std::string_view key, std::string_view value) {
    return session_->mutable_config().set(std::string(key), std::string(value));
}
Session& Client::session() { return *session_; }
const Config& Client::config() const { return session_->config(); }
uint32_t Client::app_id() const { return session_->app_id(); }

Client connect(const Config& cfg) { return Client(cfg); }

} // namespace labios
