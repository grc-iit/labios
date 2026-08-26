#include <labios/label_manager.h>

#include <algorithm>
#include <optional>
#include <tuple>

namespace labios {

LabelManager::LabelManager(ContentManager& content, CatalogManager& catalog,
                           transport::NatsConnection& nats,
                           uint64_t max_label_size, uint32_t app_id,
                           int reply_timeout_ms)
    : content_(content), catalog_(catalog), nats_(nats),
      max_label_size_(max_label_size), app_id_(app_id),
      reply_timeout_ms_(reply_timeout_ms) {}

void LabelManager::register_reply(
    uint64_t label_id, std::shared_ptr<transport::AsyncReply> reply) {
    std::lock_guard lock(replies_mu_);
    replies_[label_id] = std::move(reply);
}

void LabelManager::drain_reply(uint64_t label_id) {
    std::shared_ptr<transport::AsyncReply> reply;
    {
        std::lock_guard lock(replies_mu_);
        const auto it = replies_.find(label_id);
        if (it == replies_.end()) return;
        reply = it->second;
    }

    std::vector<std::byte> data;
    {
        std::lock_guard lock(reply->mu);
        if (!reply->completed) return;
        data = reply->data;
    }
    if (!data.empty()) {
        try {
            catalog_.set_completion(deserialize_completion(data));
            completion_cv_.notify_all();
        } catch (const LabelDecodeError&) {
            // Invalid notifications never override the catalog authority.
        }
    }
    std::lock_guard lock(replies_mu_);
    replies_.erase(label_id);
}

uint64_t LabelManager::label_count(uint64_t data_size) const {
    if (data_size == 0) return 0;
    return (data_size + max_label_size_ - 1) / max_label_size_;
}

std::vector<uint64_t> LabelManager::publish_write(
    std::string_view filepath, uint64_t offset,
    std::span<const std::byte> data) {
    uint64_t remaining = data.size();
    uint64_t pos = 0;
    uint64_t previous_label_id = 0;
    std::vector<uint64_t> pending;

    while (remaining > 0) {
        const uint64_t chunk_size = std::min(remaining, max_label_size_);
        const auto chunk = data.subspan(pos, chunk_size);

        LabelData label;
        label.id = generate_label_id(app_id_);
        label.type = LabelType::Write;
        label.source = memory_ptr(chunk.data(), chunk_size);
        label.destination = file_path(filepath, offset + pos, chunk_size);
        label.operation = "write";
        label.file_key = std::string(filepath);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        label.input_binding.provenance = BindingProvenance::MaterializedSource;
        label.input_binding.content_id = std::to_string(label.id);
        label.input_binding.logical_length = chunk_size;
        label.has_input_binding = true;
        if (previous_label_id != 0) {
            label.declared_dependencies.push_back(previous_label_id);
        }
        normalize_label_resources(label);
        mark_label_created(label);
        validate_label_admission(label);
        auto [reply_to, reply] = nats_.create_reply_inbox();
        label.reply_to = std::move(reply_to);
        const auto serialized = serialize_label(label);

        content_.stage(label.id, chunk);
        catalog_.create(label);
        nats_.publish_durable("labios.labels", serialized);
        pending.push_back(label.id);
        previous_label_id = label.id;
        register_reply(label.id, std::move(reply));

        pos += chunk_size;
        remaining -= chunk_size;
    }

    nats_.flush();
    return pending;
}

std::vector<uint64_t> LabelManager::publish_read(
    std::string_view filepath, uint64_t offset, uint64_t size) {
    uint64_t remaining = size;
    uint64_t pos = 0;
    std::vector<uint64_t> pending;

    while (remaining > 0) {
        const uint64_t chunk_size = std::min(remaining, max_label_size_);

        LabelData label;
        label.id = generate_label_id(app_id_);
        label.type = LabelType::Read;
        label.source = file_path(filepath, offset + pos, chunk_size);
        label.destination = memory_ptr(nullptr, chunk_size);
        label.operation = "read";
        label.file_key = std::string(filepath);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        normalize_label_resources(label);
        mark_label_created(label);
        validate_label_admission(label);
        auto [reply_to, reply] = nats_.create_reply_inbox();
        label.reply_to = std::move(reply_to);
        const auto serialized = serialize_label(label);

        catalog_.create(label);
        nats_.publish_durable("labios.labels", serialized);
        pending.push_back(label.id);
        register_reply(label.id, std::move(reply));

        pos += chunk_size;
        remaining -= chunk_size;
    }

    nats_.flush();
    return pending;
}

namespace {

std::pair<std::string, std::string> split_error(std::string_view error) {
    const auto separator = error.find(':');
    if (separator == std::string_view::npos) {
        return {"EXECUTION_FAILED", std::string(error)};
    }
    auto detail = error.substr(separator + 1);
    while (!detail.empty() && detail.front() == ' ') detail.remove_prefix(1);
    return {std::string(error.substr(0, separator)), std::string(detail)};
}

CompletionResult completion_view(uint64_t id, const CompletionData& completion) {
    CompletionResult result;
    result.label_id = id;
    result.state = completion.status == CompletionStatus::Complete
        ? CompletionState::Complete
        : (completion.error.rfind("CANCELED:", 0) == 0
               ? CompletionState::Cancelled : CompletionState::Failed);
    if (!completion.error.empty()) {
        auto [category, detail] = split_error(completion.error);
        result.category = std::move(category);
        result.error = std::move(detail);
    }
    result.data_key = completion.data_key;
    result.observation_version = completion.observation_version;
    result.worker_id = completion.worker_id;
    result.attempt = completion.attempt;
    result.queued_us = completion.queued_us;
    result.dispatched_us = completion.dispatched_us;
    result.started_us = completion.started_us;
    result.completed_us = completion.completed_us;
    result.queue_delay_us = completion.queue_delay_us;
    result.service_time_us = completion.service_time_us;
    result.lifecycle = result.state == CompletionState::Complete
        ? LifecycleState::Completed
        : (result.state == CompletionState::Cancelled
               ? LifecycleState::Cancelled : LifecycleState::Failed);
    return result;
}

} // namespace

WaitResult LabelManager::wait(std::span<const uint64_t> label_ids,
                              std::chrono::milliseconds timeout) {
    return wait_all(label_ids, timeout);
}

CompletionResult LabelManager::test(uint64_t label_id) {
    if (label_id == 0) {
        return {label_id, CompletionState::Unknown, "LOOKUP_FAILED",
                "label ID must be nonzero", {}};
    }
    drain_reply(label_id);
    if (auto completion = catalog_.get_completion(label_id)) {
        return completion_view(label_id, *completion);
    }
    try {
        const auto status = catalog_.get_status(label_id);
        CompletionResult result{label_id, CompletionState::Pending, {}, {}, {}};
        switch (status) {
            case LabelStatus::Submitted:
                result.lifecycle = LifecycleState::Submitted;
                break;
            case LabelStatus::Queued:
                result.lifecycle = LifecycleState::Queued;
                if (const auto snapshot = catalog_.get_snapshot(label_id);
                    snapshot && snapshot->status == StatusCode::Shuffled) {
                    result.lifecycle = LifecycleState::Shuffled;
                }
                break;
            case LabelStatus::Parked:
                result.state = CompletionState::Parked;
                result.lifecycle = LifecycleState::Parked;
                if (const auto parking = catalog_.get_parking_info(label_id)) {
                    result.park_reason = parking->reason;
                    result.park_attempts = parking->attempts;
                    result.next_retry_at_ms = parking->next_retry_at_ms;
                }
                break;
            case LabelStatus::Scheduled:
                result.lifecycle = LifecycleState::Scheduled;
                break;
            case LabelStatus::Executing:
                result.lifecycle = LifecycleState::Executing;
                break;
            case LabelStatus::Complete:
                result.state = CompletionState::Complete;
                result.lifecycle = LifecycleState::Completed;
                break;
            case LabelStatus::Error:
                result.state = CompletionState::Failed;
                result.lifecycle = LifecycleState::Failed;
                result.category = "EXECUTION_FAILED";
                result.error = catalog_.get_error(label_id).value_or(
                    "terminal catalog state has no retained completion");
                break;
            case LabelStatus::Cancelled:
                result.state = CompletionState::Cancelled;
                result.lifecycle = LifecycleState::Cancelled;
                result.category = "CANCELED";
                break;
        }
        return result;
    } catch (const std::exception&) {
        return {label_id, CompletionState::Unknown, "LOOKUP_FAILED",
                "label is unknown or its completion retention expired", {}};
    }
}

CompletionResult LabelManager::wait_one(
    uint64_t label_id, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto result = test(label_id);
        if (result.terminal() || result.state == CompletionState::Unknown) {
            return result;
        }
        if (timeout.count() == 0 || std::chrono::steady_clock::now() >= deadline) {
            result.state = CompletionState::Timeout;
            return result;
        }
        std::unique_lock lock(completion_mu_);
        completion_cv_.wait_for(lock, std::chrono::milliseconds(25));
    }
}

WaitResult LabelManager::wait_any(std::span<const uint64_t> ids,
                                  std::chrono::milliseconds timeout) {
    if (ids.empty()) return {CompletionState::Complete, {}};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        std::optional<CompletionResult> selected;
        for (const auto id : ids) {
            auto candidate = test(id);
            if (candidate.state == CompletionState::Unknown) {
                return {CompletionState::Unknown, {std::move(candidate)}};
            }
            if (!candidate.terminal()) continue;
            if (!selected ||
                std::tie(candidate.completed_us, candidate.label_id) <
                    std::tie(selected->completed_us, selected->label_id)) {
                selected = std::move(candidate);
            }
        }
        if (selected) return {selected->state, {std::move(*selected)}};
        if (timeout.count() == 0 || std::chrono::steady_clock::now() >= deadline) {
            return {CompletionState::Timeout, {}};
        }
        std::unique_lock lock(completion_mu_);
        completion_cv_.wait_for(lock, std::chrono::milliseconds(25));
    }
}

WaitResult LabelManager::wait_all(std::span<const uint64_t> ids,
                                  std::chrono::milliseconds timeout) {
    if (ids.empty()) return {CompletionState::Complete, {}};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        WaitResult out{CompletionState::Complete, {}};
        out.results.reserve(ids.size());
        bool all_terminal = true;
        for (const auto id : ids) {
            auto result = test(id);
            all_terminal = all_terminal && result.terminal();
            if (result.state == CompletionState::Unknown) {
                out.state = CompletionState::Unknown;
            } else if (result.state == CompletionState::Failed &&
                       (out.state == CompletionState::Complete ||
                        out.state == CompletionState::Cancelled)) {
                out.state = CompletionState::Failed;
            } else if (result.state == CompletionState::Cancelled &&
                       out.state == CompletionState::Complete) {
                out.state = CompletionState::Cancelled;
            }
            out.results.push_back(std::move(result));
        }
        if (all_terminal || out.state == CompletionState::Unknown) return out;
        if (timeout.count() == 0 || std::chrono::steady_clock::now() >= deadline) {
            out.state = CompletionState::Timeout;
            return out; // Nonterminal members retain Pending/Parked state.
        }
        std::unique_lock lock(completion_mu_);
        completion_cv_.wait_for(lock, std::chrono::milliseconds(25));
    }
}

CancellationResult LabelManager::cancel(uint64_t label_id) {
    CancellationResult out;
    out.label_id = label_id;
    try {
        if (catalog_.cancel_if_pre_execution(label_id)) {
            completion_cv_.notify_all();
            out.state = CancellationState::Cancelled;
            out.completion = test(label_id);
            return out;
        }
        out.completion = test(label_id);
        if (out.completion.state == CompletionState::Unknown) {
            out.state = CancellationState::Unknown;
        } else if (out.completion.terminal()) {
            out.state = out.completion.state == CompletionState::Cancelled
                ? CancellationState::Cancelled : CancellationState::Terminal;
        } else {
            out.state = CancellationState::TooLate;
        }
    } catch (const std::exception&) {
        out.state = CancellationState::Unknown;
        out.completion = {label_id, CompletionState::Unknown, "LOOKUP_FAILED",
                          "unable to query label lifecycle", {}};
    }
    return out;
}

std::vector<std::byte> LabelManager::wait_read(
    std::span<const uint64_t> label_ids, std::chrono::milliseconds timeout) {
    const auto waited = wait_all(label_ids, timeout);
    if (waited.state == CompletionState::Timeout) {
        const auto pending_id = waited.results.empty()
            ? uint64_t{0} : waited.results.front().label_id;
        throw CompletionError(
            CompletionState::Timeout, pending_id,
            "timed out waiting for read labels; the operation remains active");
    }

    std::vector<std::byte> result;
    for (const auto& completion : waited.results) {
        if (completion.state != CompletionState::Complete) {
            throw CompletionError(
                completion.state, completion.label_id,
                "read label " + std::to_string(completion.label_id) +
                    " did not complete: " + completion.category + ": " +
                    completion.error);
        }
        const auto key = completion.data_key.empty()
            ? ContentManager::data_key(completion.label_id)
            : completion.data_key;
        const auto data = content_.retrieve_key(key);
        result.insert(result.end(), data.begin(), data.end());
    }
    return result; // Retrieval is repeatable; explicit release is separate.
}

} // namespace labios
