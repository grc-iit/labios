#include <labios/label_manager.h>

#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace labios {

LabelManager::LabelManager(ContentManager& content, CatalogManager& catalog,
                           transport::NatsConnection& nats,
                           uint64_t max_label_size, uint32_t app_id,
                           int reply_timeout_ms)
    : content_(content), catalog_(catalog), nats_(nats),
      max_label_size_(max_label_size), app_id_(app_id),
      reply_timeout_ms_(reply_timeout_ms) {}

uint64_t LabelManager::label_count(uint64_t data_size) const {
    if (data_size == 0) return 0;
    return (data_size + max_label_size_ - 1) / max_label_size_;
}

std::vector<PendingLabel> LabelManager::publish_write(
    std::string_view filepath, uint64_t offset,
    std::span<const std::byte> data) {

    uint64_t remaining = data.size();
    uint64_t pos = 0;
    std::vector<PendingLabel> pending;

    while (remaining > 0) {
        uint64_t chunk_size = std::min(remaining, max_label_size_);
        auto chunk = data.subspan(pos, chunk_size);

        LabelData label;
        label.id = generate_label_id(app_id_);
        label.type = LabelType::Write;
        label.source = memory_ptr(chunk.data(), chunk_size);
        label.destination = file_path(filepath, offset + pos, chunk_size);
        label.operation = "write";
        label.flags = 0;
        label.file_key = std::string(filepath);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        label.input_binding.provenance = BindingProvenance::MaterializedSource;
        label.input_binding.content_id = std::to_string(label.id);
        label.input_binding.logical_length = chunk_size;
        label.has_input_binding = true;
        normalize_label_resources(label);
        mark_label_created(label);
        validate_label_admission(label);
        auto [reply_to, async] = nats_.create_reply_inbox();
        label.reply_to = std::move(reply_to);
        auto serialized = serialize_label(label);

        content_.stage(label.id, chunk);
        catalog_.create(label);

        nats_.publish_durable("labios.labels", serialized);
        pending.push_back({label.id, {}, std::move(async)});

        pos += chunk_size;
        remaining -= chunk_size;
    }

    nats_.flush();
    return pending;
}

std::vector<PendingLabel> LabelManager::publish_read(
    std::string_view filepath, uint64_t offset, uint64_t size) {

    uint64_t remaining = size;
    uint64_t pos = 0;
    std::vector<PendingLabel> pending;

    while (remaining > 0) {
        uint64_t chunk_size = std::min(remaining, max_label_size_);

        LabelData label;
        label.id = generate_label_id(app_id_);
        label.type = LabelType::Read;
        label.source = file_path(filepath, offset + pos, chunk_size);
        label.destination = memory_ptr(nullptr, chunk_size);
        label.operation = "read";
        label.flags = 0;
        label.file_key = std::string(filepath);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        normalize_label_resources(label);
        mark_label_created(label);
        validate_label_admission(label);
        auto [reply_to, async] = nats_.create_reply_inbox();
        label.reply_to = std::move(reply_to);
        auto serialized = serialize_label(label);

        catalog_.create(label);

        nats_.publish_durable("labios.labels", serialized);
        pending.push_back({label.id, {}, std::move(async)});

        pos += chunk_size;
        remaining -= chunk_size;
    }

    nats_.flush();
    return pending;
}

static void resolve_reply(PendingLabel& p, int timeout_ms) {
    if (p.async_reply && p.reply_data.empty()) {
        p.reply_data = p.async_reply->wait(std::chrono::milliseconds(timeout_ms));
        p.async_reply.reset();
    }
}

static CompletionResult view(uint64_t id, const CompletionData& c) {
    return {id, c.status == CompletionStatus::Complete
                    ? CompletionState::Complete
                    : (c.error.rfind("CANCELED:", 0) == 0
                           ? CompletionState::Cancelled : CompletionState::Failed),
            c.error, c.data_key};
}

CompletionResult LabelManager::test(uint64_t label_id) {
    if (auto completion = catalog_.get_completion(label_id))
        return view(label_id, *completion);
    try {
        auto status = catalog_.get_status(label_id);
        if (status == LabelStatus::Parked) {
            return {label_id, CompletionState::Parked, {}, {}};
        }
    } catch (const std::exception&) {
        // A handle can be observed before admission creates its catalog row.
    }
    return {label_id, CompletionState::Pending, {}, {}};
}

CompletionResult LabelManager::wait_one(uint64_t label_id,
                                         std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        auto result = test(label_id);
        if (result.terminal()) return result;
        if (timeout.count() == 0 || std::chrono::steady_clock::now() >= deadline) {
            result.state = CompletionState::Timeout;
            return result;
        }
        std::unique_lock lock(completion_mu_);
        completion_cv_.wait_for(lock, std::min(std::chrono::milliseconds(25),
                                                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())));
    }
}

WaitResult LabelManager::wait_any(std::span<const uint64_t> ids,
                                   std::chrono::milliseconds timeout) {
    if (ids.empty()) return {CompletionState::Complete, {}};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        for (auto id : ids) {
            auto result = test(id);
            if (result.terminal()) return {result.state, {std::move(result)}};
        }
        if (timeout.count() == 0 || std::chrono::steady_clock::now() >= deadline)
            return {CompletionState::Timeout, {}};
        std::unique_lock lock(completion_mu_);
        completion_cv_.wait_for(lock, std::chrono::milliseconds(25));
    }
}

WaitResult LabelManager::wait_all(std::span<const uint64_t> ids,
                                   std::chrono::milliseconds timeout) {
    WaitResult out{CompletionState::Complete, {}};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (auto id : ids) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        auto result = wait_one(id, std::max(std::chrono::milliseconds(0), remaining));
        out.results.push_back(result);
        if (result.state == CompletionState::Timeout) out.state = CompletionState::Timeout;
        else if (result.state == CompletionState::Failed || result.state == CompletionState::Cancelled)
            out.state = result.state;
    }
    return out;
}

bool LabelManager::cancel(uint64_t label_id) {
    try {
        auto cancelled = catalog_.cancel_if_pre_execution(label_id);
        if (cancelled) completion_cv_.notify_all();
        return cancelled;
    } catch (const std::exception&) {
        return false;
    }
}

WaitResult LabelManager::wait(std::span<PendingLabel> pending,
                              std::chrono::milliseconds timeout) {
    std::vector<uint64_t> ids;
    ids.reserve(pending.size());
    for (auto& p : pending) {
        resolve_reply(p, std::min<int64_t>(timeout.count(), reply_timeout_ms_));
        if (!p.reply_data.empty()) {
            try {
                catalog_.set_completion(deserialize_completion(p.reply_data));
                completion_cv_.notify_all();
            } catch (const LabelDecodeError&) { /* catalog remains authoritative */ }
        }
        ids.push_back(p.label_id);
    }
    return wait_all(ids, timeout);
}

std::vector<std::byte> LabelManager::wait_read(
    std::span<PendingLabel> pending) {

    std::vector<std::byte> result;
    for (auto& p : pending) {
        resolve_reply(p, reply_timeout_ms_);
        if (p.reply_data.empty()) continue;
        CompletionData comp;
        try {
            comp = deserialize_completion(p.reply_data);
            catalog_.set_completion(comp);
            completion_cv_.notify_all();
        } catch (const LabelDecodeError& ex) {
            std::cerr << "label manager: rejected completion (" << ex.category()
                      << "): " << ex.what() << "\n" << std::flush;
            continue;
        }
        if (comp.status == CompletionStatus::Error) {
            throw std::runtime_error("read label " + std::to_string(p.label_id)
                                     + " failed: " + comp.error);
        }
        auto key = comp.data_key.empty()
            ? ContentManager::data_key(p.label_id)
            : comp.data_key;
        auto data = content_.retrieve_key(key);
        result.insert(result.end(), data.begin(), data.end());
        content_.remove_key(key);
    }
    return result;
}

} // namespace labios
