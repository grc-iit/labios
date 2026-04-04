#pragma once

#include <labios/config.h>
#include <labios/label.h>
#include <labios/label_manager.h>

#include <cstddef>
#include <cstdint>
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

    Session& session();
    const Config& config() const;
    uint32_t app_id() const;

private:
    std::unique_ptr<Session> session_;
};

Client connect(const Config& cfg);

} // namespace labios
