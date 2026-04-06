#include <labios/label_manager.h>

#include <algorithm>
#include <stdexcept>

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
        label.flags = LabelFlags::Queued;
        label.file_key = std::string(filepath);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        mark_label_created(label);
        auto serialized = serialize_label(label);

        content_.stage(label.id, chunk);
        catalog_.create(label);

        auto async = nats_.publish_request_async("labios.labels", serialized);
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
        label.flags = LabelFlags::Queued;
        label.file_key = std::string(filepath);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        mark_label_created(label);
        auto serialized = serialize_label(label);

        catalog_.create(label);

        auto async = nats_.publish_request_async("labios.labels", serialized);
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

void LabelManager::wait(std::span<PendingLabel> pending) {
    for (auto& p : pending) {
        resolve_reply(p, reply_timeout_ms_);
        if (p.reply_data.empty()) continue;
        auto comp = deserialize_completion(p.reply_data);
        if (comp.status == CompletionStatus::Error) {
            throw std::runtime_error("label " + std::to_string(p.label_id)
                                     + " failed: " + comp.error);
        }
    }
}

std::vector<std::byte> LabelManager::wait_read(
    std::span<PendingLabel> pending) {

    std::vector<std::byte> result;
    for (auto& p : pending) {
        resolve_reply(p, reply_timeout_ms_);
        if (p.reply_data.empty()) continue;
        auto comp = deserialize_completion(p.reply_data);
        if (comp.status == CompletionStatus::Error) {
            throw std::runtime_error("read label " + std::to_string(p.label_id)
                                     + " failed: " + comp.error);
        }
        auto data = content_.retrieve(p.label_id);
        result.insert(result.end(), data.begin(), data.end());
        content_.remove(p.label_id);
    }
    return result;
}

} // namespace labios
