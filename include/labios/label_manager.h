#pragma once

#include <labios/catalog_manager.h>
#include <labios/content_manager.h>
#include <labios/label.h>
#include <labios/transport/nats.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace labios {

enum class CompletionState : uint8_t {
    Pending, Complete, Failed, Cancelled, Parked, Timeout, Unknown
};

enum class CancellationState : uint8_t {
    Cancelled, TooLate, Terminal, Unknown
};

enum class LifecycleState : uint8_t {
    Submitted, Admitted, Queued, Parked, Shuffled, Scheduled, Executing,
    Completed, Failed, Cancelled, Unknown
};

/// Typed failure from a synchronous completion wait. A Timeout means only that
/// this wait ended; the label remains active until it completes or is explicitly
/// cancelled.
class CompletionError : public std::runtime_error {
public:
    CompletionError(CompletionState state, uint64_t label_id, std::string message)
        : std::runtime_error(std::move(message)), state_(state), label_id_(label_id) {}

    CompletionState state() const noexcept { return state_; }
    uint64_t label_id() const noexcept { return label_id_; }

private:
    CompletionState state_;
    uint64_t label_id_;
};

struct CompletionResult {
    uint64_t label_id = 0;
    CompletionState state = CompletionState::Pending;
    std::string category;
    std::string error;
    std::string data_key;
    uint32_t observation_version = 0;
    int worker_id = -1;
    uint32_t attempt = 0;
    uint64_t queued_us = 0;
    uint64_t dispatched_us = 0;
    uint64_t started_us = 0;
    uint64_t completed_us = 0;
    uint64_t queue_delay_us = 0;
    uint64_t service_time_us = 0;
    std::string park_reason;
    uint64_t park_attempts = 0;
    uint64_t next_retry_at_ms = 0;
    LifecycleState lifecycle = LifecycleState::Unknown;

    bool has_execution_observation() const {
        return observation_version == 1;
    }

    bool terminal() const {
        return state == CompletionState::Complete ||
               state == CompletionState::Failed ||
               state == CompletionState::Cancelled;
    }
};

struct WaitResult {
    CompletionState state = CompletionState::Pending;
    std::vector<CompletionResult> results;
};

struct CancellationResult {
    uint64_t label_id = 0;
    CancellationState state = CancellationState::Unknown;
    CompletionResult completion;
};

class LabelManager {
public:
    LabelManager(ContentManager& content, CatalogManager& catalog,
                 transport::NatsConnection& nats,
                 uint64_t max_label_size, uint32_t app_id,
                 int reply_timeout_ms = 30000);

    std::vector<uint64_t> publish_write(
        std::string_view filepath, uint64_t offset,
        std::span<const std::byte> data);

    std::vector<uint64_t> publish_read(
        std::string_view filepath, uint64_t offset, uint64_t size);

    WaitResult wait(std::span<const uint64_t> label_ids,
                    std::chrono::milliseconds timeout =
                        std::chrono::milliseconds(30000));
    CompletionResult test(uint64_t label_id);
    CompletionResult wait_one(uint64_t label_id,
                              std::chrono::milliseconds timeout);
    WaitResult wait_any(std::span<const uint64_t> label_ids,
                        std::chrono::milliseconds timeout);
    WaitResult wait_all(std::span<const uint64_t> label_ids,
                        std::chrono::milliseconds timeout);
    CancellationResult cancel(uint64_t label_id);

    std::vector<std::byte> wait_read(
        std::span<const uint64_t> label_ids,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));

    /// Register a transport notification hint. Access is synchronized and the
    /// persisted catalog remains authoritative.
    void register_reply(uint64_t label_id,
                        std::shared_ptr<transport::AsyncReply> reply);

    uint64_t label_count(uint64_t data_size) const;

private:
    ContentManager& content_;
    CatalogManager& catalog_;
    transport::NatsConnection& nats_;
    uint64_t max_label_size_;
    uint32_t app_id_;
    int reply_timeout_ms_;
    mutable std::mutex completion_mu_;
    std::condition_variable completion_cv_;
    mutable std::mutex replies_mu_;
    std::unordered_map<uint64_t, std::shared_ptr<transport::AsyncReply>> replies_;

    void drain_reply(uint64_t label_id);
};

} // namespace labios
