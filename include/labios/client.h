#pragma once

#include <labios/channel.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/label_manager.h>
#include <labios/sds/types.h>
#include <labios/workspace.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

class Session;
namespace detail {
class OperationContext;
class OperationFactory;
} // namespace detail

enum class OperationKind : uint8_t { Generic, Read };
enum class ClientErrorCode : uint8_t {
    InvalidArgument, LookupFailed, SessionShutdown, ProtocolError, SubmissionFailed
};

class ClientError : public std::runtime_error {
public:
    ClientError(ClientErrorCode code, std::string category, std::string message)
        : std::runtime_error(std::move(message)), code_(code),
          category_(std::move(category)) {}
    ClientErrorCode code() const noexcept { return code_; }
    const std::string& category() const noexcept { return category_; }
private:
    ClientErrorCode code_;
    std::string category_;
};

/// Owning asynchronous Label I/O operation.
///
/// Copies share the session needed to query the catalog. Label IDs and operation
/// kind are immutable. All const observation methods and cancel() may be called
/// concurrently. Destroying the originating Client neither cancels nor
/// invalidates this handle; the shared session closes after its final handle is
/// released.
class Operation {
public:
    Operation() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return label_ids_.empty(); }
    [[nodiscard]] OperationKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::span<const uint64_t> label_ids() const noexcept {
        return label_ids_;
    }
    [[nodiscard]] uint64_t label_id(size_t index = 0) const;

    CompletionResult test(size_t index = 0) const;
    WaitResult wait_for(std::chrono::milliseconds timeout) const;
    WaitResult wait_any(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) const;
    WaitResult wait_all(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) const;
    WaitResult wait() const;
    std::vector<CancellationResult> cancel() const;
    std::vector<std::byte> read(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) const;

private:
    friend class detail::OperationFactory;
    Operation(std::shared_ptr<detail::OperationContext> context,
              std::vector<uint64_t> label_ids, OperationKind kind);

    std::shared_ptr<detail::OperationContext> context_;
    std::vector<uint64_t> label_ids_;
    OperationKind kind_ = OperationKind::Generic;
};

/// Compatibility name retained for source code that treated PendingIO as an
/// opaque value. Direct access to the former mutable `pending` vector is removed.
using PendingIO = Operation; // Deprecated compatibility spelling; use Operation.

/// Owning process-local channel handle. The underlying registry and sequence
/// state are process-local in 2.1; this type makes no cross-process coordination
/// claim. Copies are thread-safe to use according to Channel's method contract.
class ChannelHandle {
public:
    ChannelHandle() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return channel_ != nullptr; }
    const std::string& name() const;
    uint64_t publish(std::span<const std::byte> data, uint64_t label_id = 0) const;
    int subscribe(ChannelCallback callback) const;
    void unsubscribe(int subscription_id) const;
    int subscriber_count() const;
    void drain() const;
    void destroy() const;
    bool is_destroyed() const;
private:
    friend class Client;
    ChannelHandle(std::shared_ptr<Session> session, std::shared_ptr<Channel> channel)
        : session_(std::move(session)), channel_(std::move(channel)) {}
    std::shared_ptr<Session> session_;
    std::shared_ptr<Channel> channel_;
};

/// Owning process-local workspace handle. ACL and registry identity remain
/// process-local in 2.1. Calls after destroy fail with a stable exception.
class WorkspaceHandle {
public:
    WorkspaceHandle() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return workspace_ != nullptr; }
    const std::string& name() const;
    uint32_t owner() const;
    uint64_t put(std::string_view key, std::span<const std::byte> data) const;
    std::optional<std::vector<std::byte>> get(std::string_view key) const;
    std::optional<std::vector<std::byte>> get_version(
        std::string_view key, uint64_t version) const;
    bool erase(std::string_view key) const;
    std::vector<WorkspaceEntry> list(std::string_view prefix = {}) const;
    void grant(uint32_t app_id) const;
    void revoke(uint32_t app_id) const;
    void destroy() const;
    bool is_destroyed() const;
private:
    friend class Client;
    WorkspaceHandle(std::shared_ptr<Session> session,
                    std::shared_ptr<Workspace> workspace, uint32_t app_id)
        : session_(std::move(session)), workspace_(std::move(workspace)),
          app_id_(app_id) {}
    std::shared_ptr<Session> session_;
    std::shared_ptr<Workspace> workspace_;
    uint32_t app_id_ = 0;
};

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    void write(std::string_view filepath, std::span<const std::byte> data,
               uint64_t offset = 0);
    std::vector<std::byte> read(std::string_view filepath, uint64_t offset,
                                uint64_t size);

    Operation async_write(std::string_view filepath,
                          std::span<const std::byte> data,
                          uint64_t offset = 0);
    Operation async_read(std::string_view filepath, uint64_t offset, uint64_t size);

    CompletionResult test(const Operation& operation) const;
    void wait(const Operation& operation) const;
    WaitResult wait_for(const Operation& operation,
                        std::chrono::milliseconds timeout) const;
    WaitResult wait_any(std::span<const uint64_t> label_ids,
                        std::chrono::milliseconds timeout =
                            std::chrono::milliseconds(30000)) const;
    WaitResult wait_all(std::span<const uint64_t> label_ids,
                        std::chrono::milliseconds timeout =
                            std::chrono::milliseconds(30000)) const;
    /// Compatibility wrapper: true only when cancellation won or had already won.
    bool cancel(uint64_t label_id) const;
    CancellationResult cancel_label(uint64_t label_id) const;
    std::vector<std::byte> wait_read(const Operation& operation) const;

    /// Reconstruct a catalog-backed operation from retained label IDs.
    Operation operation(std::span<const uint64_t> label_ids,
                        OperationKind kind = OperationKind::Generic) const;

    LabelData create_label(const LabelParams& params);
    Operation publish(const LabelData& label,
                      std::span<const std::byte> data = {});

    ChannelHandle create_channel(std::string_view name,
                                 uint32_t ttl_seconds = 0);
    ChannelHandle get_channel(std::string_view name);
    uint64_t publish_to_channel(std::string_view channel_name,
                                std::span<const std::byte> data,
                                uint64_t label_id = 0);
    int subscribe_to_channel(std::string_view channel_name, ChannelCallback cb);
    void unsubscribe_from_channel(std::string_view channel_name, int sub_id);

    WorkspaceHandle create_workspace(std::string_view name,
                                     uint32_t ttl_seconds = 0);
    WorkspaceHandle get_workspace(std::string_view name);
    uint64_t workspace_put(std::string_view workspace, std::string_view key,
                           std::span<const std::byte> data);
    std::optional<std::vector<std::byte>> workspace_get(
        std::string_view workspace, std::string_view key);
    bool workspace_del(std::string_view workspace, std::string_view key);
    void workspace_grant(std::string_view workspace, uint32_t app_id);

    std::string observe(std::string_view query);
    /// Return the catalog-authoritative admitted label and runtime residual.
    /// This is the public placement-history surface; callers do not address
    /// catalog keys or transport subjects directly.
    LabelData inspect_label(uint64_t label_id);

    void write_to(std::string_view dest_uri, std::span<const std::byte> data);
    Operation async_write_to(std::string_view dest_uri,
                             std::span<const std::byte> data);
    std::vector<std::byte> read_from(std::string_view source_uri, uint64_t size);
    Operation async_read_from(std::string_view source_uri, uint64_t size);

    Operation write_with_intent(std::string_view filepath,
                                std::span<const std::byte> data,
                                Intent intent, uint8_t priority = 0);
    Operation execute_pipeline(std::string_view source_uri,
                               std::string_view dest_uri,
                               const sds::Pipeline& pipeline,
                               Intent intent = Intent::None);

    std::string get_config();
    /// Changes only this process-local client Config; it is not distributed.
    bool set_config(std::string_view key, std::string_view value);

    Session& session();
    const Config& config() const;
    uint32_t app_id() const;

private:
    std::shared_ptr<Session> session_;
    std::shared_ptr<detail::OperationContext> operation_context_;
    std::shared_ptr<ChannelRegistry> channels_;
    std::shared_ptr<WorkspaceRegistry> workspaces_;
};

Client connect(const Config& cfg);

} // namespace labios
