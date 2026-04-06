#pragma once

#include <labios/channel.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/label_manager.h>
#include <labios/sds/types.h>
#include <labios/workspace.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

class Session;  // Forward declare

/// Opaque handle for a pending async operation.
/// Returned by async publish methods. Pass to wait() to block for completion.
struct PendingIO {
    std::vector<PendingLabel> pending;
};

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    // --- Synchronous convenience API ---
    void write(std::string_view filepath, std::span<const std::byte> data,
               uint64_t offset = 0);
    std::vector<std::byte> read(std::string_view filepath, uint64_t offset,
                                uint64_t size);

    // --- Asynchronous label API (paper Figure 4) ---

    /// Publish a write asynchronously. Returns immediately.
    /// The data is staged in the warehouse and labels are queued.
    /// Call wait() to block until all labels complete.
    PendingIO async_write(std::string_view filepath,
                            std::span<const std::byte> data,
                            uint64_t offset = 0);

    /// Publish a read asynchronously. Returns immediately.
    /// Call wait_read() to block and retrieve the data.
    PendingIO async_read(std::string_view filepath,
                           uint64_t offset, uint64_t size);

    /// Block until all labels in the status are complete.
    void wait(PendingIO& status);

    /// Block until read labels complete and return the data.
    std::vector<std::byte> wait_read(PendingIO& status);

    // --- Label-level API (advanced, paper Section 3.2.3) ---

    /// Create a label from parameters without publishing it.
    /// The label gets a unique ID and is ready to publish.
    LabelData create_label(const LabelParams& params);

    /// Publish a pre-built label asynchronously.
    PendingIO publish(const LabelData& label,
                        std::span<const std::byte> data = {});

    // --- Channel API (streaming coordination) ---

    /// Create a named channel for streaming between agents.
    Channel* create_channel(std::string_view name, uint32_t ttl_seconds = 0);

    /// Get an existing channel by name.
    Channel* get_channel(std::string_view name);

    /// Publish data to a named channel. Returns the sequence number (0 on error).
    uint64_t publish_to_channel(std::string_view channel_name,
                                std::span<const std::byte> data,
                                uint64_t label_id = 0);

    /// Subscribe to a named channel. Returns subscription ID (-1 on error).
    int subscribe_to_channel(std::string_view channel_name, ChannelCallback cb);

    /// Unsubscribe from a channel.
    void unsubscribe_from_channel(std::string_view channel_name, int sub_id);

    // --- Workspace API (persistent shared state) ---

    /// Create a named workspace for multi-agent shared state.
    Workspace* create_workspace(std::string_view name, uint32_t ttl_seconds = 0);

    /// Get an existing workspace by name.
    Workspace* get_workspace(std::string_view name);

    /// Write data to a key in a workspace. Returns the new version number (0 on error).
    uint64_t workspace_put(std::string_view workspace, std::string_view key,
                           std::span<const std::byte> data);

    /// Read data from a key in a workspace.
    std::optional<std::vector<std::byte>> workspace_get(
        std::string_view workspace, std::string_view key);

    /// Delete a key from a workspace.
    bool workspace_del(std::string_view workspace, std::string_view key);

    /// Grant another agent access to a workspace.
    void workspace_grant(std::string_view workspace, uint32_t app_id);

    // --- Observability API ---

    /// Query system state via an OBSERVE label. Returns JSON string.
    std::string observe(std::string_view query);

    // --- URI-based I/O ---

    /// Write data to a URI destination (file://, s3://, etc).
    void write_to(std::string_view dest_uri, std::span<const std::byte> data);

    /// Async write to a URI destination.
    PendingIO async_write_to(std::string_view dest_uri,
                              std::span<const std::byte> data);

    /// Read from a URI source.
    std::vector<std::byte> read_from(std::string_view source_uri, uint64_t size);

    /// Async read from a URI source.
    PendingIO async_read_from(std::string_view source_uri, uint64_t size);

    // --- Intent-driven convenience API ---

    /// Write with explicit intent (Checkpoint, Cache, etc).
    PendingIO write_with_intent(std::string_view filepath,
                                 std::span<const std::byte> data,
                                 Intent intent, uint8_t priority = 0);

    /// Publish a pipeline label: read from source, execute pipeline, write to dest.
    PendingIO execute_pipeline(std::string_view source_uri,
                                std::string_view dest_uri,
                                const sds::Pipeline& pipeline,
                                Intent intent = Intent::None);

    // --- Configuration ---

    /// Query current LABIOS configuration as JSON string.
    std::string get_config();

    /// Set a runtime configuration parameter. Returns false for unknown keys.
    bool set_config(std::string_view key, std::string_view value);

    Session& session();
    const Config& config() const;
    uint32_t app_id() const;

private:
    std::unique_ptr<Session> session_;
    std::unique_ptr<ChannelRegistry> channels_;
    std::unique_ptr<WorkspaceRegistry> workspaces_;
};

Client connect(const Config& cfg);

} // namespace labios
