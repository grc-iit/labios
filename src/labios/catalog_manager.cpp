#include <labios/catalog_manager.h>
#include <labios/shuffler.h>

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace labios {

namespace {

std::string now_ms() {
    auto now = std::chrono::system_clock::now();
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();
    return std::to_string(epoch_ms);
}

transport::RedisConnection::HashField text_field(
    std::string name, std::string value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return {std::move(name), {begin, begin + value.size()}};
}

transport::RedisConnection::HashField binary_field(
    std::string name, std::span<const std::byte> value) {
    return {std::move(name), {value.begin(), value.end()}};
}

} // namespace

std::string to_string(LabelStatus status) {
    switch (status) {
        case LabelStatus::Queued:    return "queued";
        case LabelStatus::Parked:    return "parked";
        case LabelStatus::Scheduled: return "scheduled";
        case LabelStatus::Executing: return "executing";
        case LabelStatus::Complete:  return "complete";
        case LabelStatus::Error:     return "error";
        case LabelStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}

uint64_t parking_backoff_ms(uint64_t attempt) noexcept {
    constexpr uint64_t initial_ms = 100;
    constexpr uint64_t maximum_ms = 60'000;
    const auto exponent = std::min<uint64_t>(attempt, 9);
    return std::min(maximum_ms, initial_ms << exponent);
}

LabelStatus label_status_from_string(std::string_view s) {
    if (s == "queued")    return LabelStatus::Queued;
    if (s == "parked")    return LabelStatus::Parked;
    if (s == "scheduled") return LabelStatus::Scheduled;
    if (s == "executing") return LabelStatus::Executing;
    if (s == "complete")  return LabelStatus::Complete;
    if (s == "error")     return LabelStatus::Error;
    if (s == "cancelled") return LabelStatus::Cancelled;
    throw std::invalid_argument(
        "unknown label status: " + std::string(s));
}

CatalogManager::CatalogManager(transport::RedisConnection& redis)
    : redis_(redis) {}

std::string CatalogManager::catalog_key(uint64_t label_id) const {
    return "labios:catalog:" + std::to_string(label_id);
}

void CatalogManager::create(uint64_t label_id, uint32_t app_id,
                             LabelType type) {
    LabelData label;
    label.id = label_id;
    label.type = type;
    label.app_id = app_id;
    create(label);
}

void CatalogManager::create(const LabelData& label) {
    admit(label);
}

void CatalogManager::admit(const LabelData& label) {
    const auto key = catalog_key(label.id);
    const auto ts = now_ms();
    const auto snapshot = serialize_label(label);
    const std::vector<transport::RedisConnection::HashField> fields{
        text_field("status", "queued"), text_field("parked", "0"),
        text_field("app_id", std::to_string(label.app_id)),
        text_field("type", std::to_string(static_cast<int>(label.type))),
        text_field("flags", std::to_string(label.flags)),
        text_field("priority", std::to_string(label.priority)),
        text_field("operation", label.operation), text_field("created_at", ts),
        text_field("updated_at", ts), text_field("last_transition_at", ts),
        binary_field("canonical_label", snapshot)};
    redis_.hset_fields(key, fields);
}

bool CatalogManager::durable_handoff(const LabelData& label) {
    const auto key = catalog_key(label.id);
    const auto ts = now_ms();
    const auto snapshot = serialize_label(label);
    const std::vector<transport::RedisConnection::HashField> fields{
        text_field("status", "queued"), text_field("parked", "0"),
        text_field("app_id", std::to_string(label.app_id)),
        text_field("type", std::to_string(static_cast<int>(label.type))),
        text_field("flags", std::to_string(label.flags)),
        text_field("priority", std::to_string(label.priority)),
        text_field("operation", label.operation), text_field("updated_at", ts),
        text_field("last_transition_at", ts),
        binary_field("canonical_label", snapshot)};
    return redis_.hset_fields_if(key, "status", "queued", true, fields);
}

void CatalogManager::persist_snapshot(const LabelData& label) {
    const auto snapshot = serialize_label(label);
    const std::vector<transport::RedisConnection::HashField> fields{
        binary_field("canonical_label", snapshot),
        text_field("updated_at", now_ms())};
    redis_.hset_fields(catalog_key(label.id), fields);
}

std::optional<LabelData> CatalogManager::get_snapshot(uint64_t label_id) {
    auto bytes = redis_.hget_binary(catalog_key(label_id), "canonical_label");
    if (bytes.empty()) {
        // Rolling compatibility with snapshots written before the catalog hash
        // became the atomic admission record.
        bytes = redis_.get_binary(catalog_key(label_id) + ":label");
    }
    if (bytes.empty()) return std::nullopt;
    try { return deserialize_label(bytes); }
    catch (...) { return std::nullopt; }
}

void CatalogManager::persist_composite(
    const LabelData& parent,
    std::span<const std::vector<std::byte>> children) {
    const auto snapshot = serialize_label(parent);
    const std::vector<std::vector<std::byte>> child_payloads(
        children.begin(), children.end());
    const auto packed = pack_labels(child_payloads);
    const auto ts = now_ms();
    const auto key = catalog_key(parent.id);
    std::vector<transport::RedisConnection::HashField> fields{
        binary_field("canonical_label", snapshot),
        binary_field("composite_program", packed),
        text_field("updated_at", ts), text_field("last_transition_at", ts)};
    if (!redis_.hget(key, "status")) {
        fields.push_back(text_field("status", "queued"));
        fields.push_back(text_field("parked", "0"));
        fields.push_back(text_field("created_at", ts));
    }
    redis_.hset_fields(key, fields);
}

std::vector<std::vector<std::byte>> CatalogManager::get_composite_program(
    uint64_t parent_id) {
    auto packed = redis_.hget_binary(catalog_key(parent_id), "composite_program");
    if (packed.empty()) return {};
    try { return unpack_labels(packed); }
    catch (...) { return {}; }
}

std::vector<LabelData> CatalogManager::recoverable_labels(uint64_t now) {
    struct Candidate { LabelData label; bool due = false; bool legacy_child = false; };
    std::vector<Candidate> candidates;
    std::unordered_set<uint64_t> committed_composite_children;
    for (const auto& key : redis_.scan_keys("labios:catalog:*")) {
        if (key.ends_with(":label") || key.ends_with(":completion")) continue;
        const auto status = redis_.hget(key, "status");
        if (!status || (*status != "queued" && *status != "parked" &&
                        *status != "scheduled")) continue;
        const auto suffix = key.substr(std::string("labios:catalog:").size());
        try {
            auto label = get_snapshot(std::stoull(suffix));
            if (!label) continue;
            bool due = true;
            if (const auto retry = redis_.hget(key, "next_retry_at"))
                due = std::stoull(*retry) <= now;
            if (label->type == LabelType::Composite) {
                committed_composite_children.insert(
                    label->children.begin(), label->children.end());
            }
            candidates.push_back({std::move(*label), due,
                                  redis_.hget(key, "composite_parent").has_value()});
        } catch (...) {
            // Malformed catalog records are isolated from the recovery scan.
        }
    }
    std::vector<LabelData> result;
    for (auto& candidate : candidates) {
        if (!candidate.due || candidate.legacy_child) continue;
        if (committed_composite_children.contains(candidate.label.id)) continue;
        result.push_back(std::move(candidate.label));
    }
    return result;
}

size_t CatalogManager::wake_parked(uint64_t now) {
    size_t count = 0;
    for (const auto& key : redis_.scan_keys("labios:catalog:*")) {
        if (key.ends_with(":label") || key.ends_with(":completion")) continue;
        const auto status = redis_.hget(key, "status");
        if (!status || *status != "parked") continue;
        redis_.hset(key, "next_retry_at", std::to_string(now));
        ++count;
    }
    return count;
}

void CatalogManager::park(const LabelData& label, std::string_view reason,
                          uint64_t attempt, uint64_t next_retry_ms,
                          std::string_view last_error) {
    const auto key = catalog_key(label.id);
    const auto snapshot = serialize_label(label);
    const auto ts = now_ms();
    const auto parked_since = redis_.hget(key, "parked_since").value_or(ts);
    const std::vector<transport::RedisConnection::HashField> fields{
        binary_field("canonical_label", snapshot), text_field("status", "parked"),
        text_field("parked", "1"), text_field("park_reason", std::string(reason)),
        text_field("park_attempts", std::to_string(attempt)),
        text_field("next_retry_at", std::to_string(next_retry_ms)),
        text_field("parked_since", parked_since), text_field("last_transition_at", ts),
        text_field("updated_at", ts), text_field("last_error", std::string(last_error))};
    redis_.hset_fields(key, fields);
}

void CatalogManager::set_composite_parent(uint64_t child_id, uint64_t composite_id) {
    redis_.hset(catalog_key(child_id), "composite_parent", std::to_string(composite_id));
}

uint64_t CatalogManager::park_attempts(uint64_t label_id) {
    auto value = redis_.hget(catalog_key(label_id), "park_attempts");
    if (!value) return 0;
    try { return std::stoull(*value); } catch (...) { return 0; }
}

DependencyReadiness CatalogManager::dependency_readiness(
    const LabelData& label) noexcept {
    for (const auto& dependency : label.dependencies) {
        try {
            if (get_status(dependency.label_id) != LabelStatus::Complete)
                return {false, "BLOCKED_PREDECESSOR"};
        } catch (...) {
            return {false, "UNKNOWN_DEPENDENCY"};
        }
    }
    return {};
}

void CatalogManager::set_status(uint64_t label_id, LabelStatus status) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "status", to_string(status));
    redis_.hset(key, "updated_at", now_ms());
}

bool CatalogManager::cancel_if_pre_execution(uint64_t label_id) {
    // The atomic cross-process compare-and-set is supplied by P07's durable
    // catalog coordinator.  This local transaction is deliberately
    // conservative: never claim cancellation once execution is observable.
    auto status = get_status(label_id);
    if (status != LabelStatus::Queued && status != LabelStatus::Parked) return false;
    set_status(label_id, LabelStatus::Cancelled);
    CompletionData completion;
    completion.label_id = label_id;
    completion.status = CompletionStatus::Error;
    completion.error = "CANCELED: cancelled before execution";
    set_completion(completion);
    return true;
}

void CatalogManager::set_completion(const CompletionData& completion) {
    auto key = catalog_key(completion.label_id);
    redis_.set_binary(key + ":completion", serialize_completion(completion));
    if (completion.status == CompletionStatus::Complete) {
        set_status(completion.label_id, LabelStatus::Complete);
    } else if (completion.error.rfind("CANCELED:", 0) == 0) {
        set_status(completion.label_id, LabelStatus::Cancelled);
    } else {
        set_status(completion.label_id, LabelStatus::Error);
        set_error(completion.label_id, completion.error);
    }
}

std::optional<CompletionData> CatalogManager::get_completion(uint64_t label_id) {
    auto bytes = redis_.get_binary(catalog_key(label_id) + ":completion");
    if (bytes.empty()) return std::nullopt;
    try {
        return deserialize_completion(bytes);
    } catch (const LabelDecodeError&) {
        return std::nullopt;
    }
}

LabelStatus CatalogManager::get_status(uint64_t label_id) {
    auto key = catalog_key(label_id);
    auto val = redis_.hget(key, "status");
    if (!val.has_value()) {
        throw std::runtime_error(
            "catalog entry not found for label " + std::to_string(label_id));
    }
    return label_status_from_string(*val);
}

void CatalogManager::set_flags(uint64_t label_id, uint32_t flags) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "flags", std::to_string(flags));
    redis_.hset(key, "updated_at", now_ms());
}

uint32_t CatalogManager::get_flags(uint64_t label_id) {
    auto key = catalog_key(label_id);
    auto val = redis_.hget(key, "flags");
    if (!val.has_value()) {
        throw std::runtime_error(
            "catalog flags not found for label " + std::to_string(label_id));
    }
    try {
        return static_cast<uint32_t>(std::stoul(*val));
    } catch (const std::exception& e) {
        std::cerr << "catalog: malformed flags value: " << e.what() << "\n";
        return 0;
    }
}

void CatalogManager::set_error(uint64_t label_id, std::string_view error) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "error", std::string(error));
    redis_.hset(key, "updated_at", now_ms());
}

std::optional<std::string> CatalogManager::get_error(uint64_t label_id) {
    return redis_.hget(catalog_key(label_id), "error");
}

void CatalogManager::set_worker(uint64_t label_id, int worker_id) {
    redis_.hset(catalog_key(label_id), "worker_id",
                std::to_string(worker_id));
}

std::optional<int> CatalogManager::get_worker(uint64_t label_id) {
    auto val = redis_.hget(catalog_key(label_id), "worker_id");
    if (!val.has_value()) {
        return std::nullopt;
    }
    try {
        return std::stoi(*val);
    } catch (const std::exception& e) {
        std::cerr << "catalog: malformed worker_id value: " << e.what() << "\n";
        return std::nullopt;
    }
}

void CatalogManager::schedule_batch(std::span<const ScheduleEntry> entries) {
    if (entries.empty()) return;

    auto ts = now_ms();
    redis_.pipeline_begin();

    for (auto& e : entries) {
        auto key = catalog_key(e.label_id);
        redis_.pipeline_hset(key, "status", "scheduled");
        redis_.pipeline_hset(key, "parked", "0");
        redis_.pipeline_hset(key, "flags", std::to_string(e.flags));
        redis_.pipeline_hset(key, "worker_id", std::to_string(e.worker_id));
        // Scheduled records become recoverable if worker delivery/claim does
        // not advance them before this dispatcher lease expires.
        redis_.pipeline_hset(key, "next_retry_at", std::to_string(
            std::stoull(ts) + 10'000ULL));
        redis_.pipeline_hset(key, "updated_at", ts);
    }

    redis_.pipeline_exec();
}

std::string CatalogManager::location_key(std::string_view filepath) {
    return "labios:location:" + std::string(filepath);
}

std::string CatalogManager::offset_location_key(std::string_view filepath) {
    return "labios:olocation:" + std::string(filepath);
}

void CatalogManager::set_location(std::string_view filepath, int worker_id) {
    redis_.set(location_key(filepath), std::to_string(worker_id));
}

std::optional<int> CatalogManager::get_location(std::string_view filepath) {
    auto val = redis_.get(location_key(filepath));
    if (!val.has_value()) {
        return std::nullopt;
    }
    try {
        return std::stoi(*val);
    } catch (const std::exception& e) {
        std::cerr << "catalog: malformed location value: " << e.what() << "\n";
        return std::nullopt;
    }
}

void CatalogManager::set_location(std::string_view filepath,
                                   uint64_t offset, uint64_t length,
                                   int worker_id) {
    // Store in a sorted set keyed by file. Score = start_offset so
    // ZRANGEBYSCORE can narrow by offset range. Member = "worker_id:end"
    // encodes the worker and the exclusive end of the range.
    auto key = offset_location_key(filepath);
    std::string member = std::to_string(worker_id) + ":"
                       + std::to_string(offset + length);
    redis_.zadd(key, static_cast<double>(offset), member);

    // Update whole-file key to the latest writer.
    redis_.set(location_key(filepath), std::to_string(worker_id));
}

std::optional<int> CatalogManager::get_location(std::string_view filepath,
                                                  uint64_t offset,
                                                  uint64_t length) {
    if (offset == 0 && length == 0) {
        return get_location(filepath);
    }

    // Score = start_offset, member = "worker_id:end". Query only entries
    // whose start_offset <= our offset (a containing range must start at
    // or before the queried offset).
    auto key = offset_location_key(filepath);
    auto entries = redis_.zrangebyscore(key, 0, static_cast<double>(offset));

    for (auto& entry : entries) {
        auto colon = entry.member.find(':');
        if (colon == std::string::npos) continue;
        try {
            int wid = std::stoi(entry.member.substr(0, colon));
            uint64_t end = std::stoull(entry.member.substr(colon + 1));
            if (offset + length <= end) {
                return wid;
            }
        } catch (const std::exception& e) {
            std::cerr << "catalog: malformed offset entry: " << e.what() << "\n";
            continue;
        }
    }

    // Fall back to whole-file location.
    return get_location(filepath);
}

std::string CatalogManager::filemeta_key(std::string_view filepath) {
    return "labios:filemeta:" + std::string(filepath);
}

void CatalogManager::track_open(std::string_view filepath, int flags) {
    auto key = filemeta_key(filepath);
    if (flags & O_CREAT) {
        redis_.hset(key, "exists", "1");
        auto existing = redis_.hget(key, "size");
        if (!existing.has_value()) {
            redis_.hset(key, "size", "0");
        }
        redis_.hset(key, "mtime", now_ms());
    }
    if (flags & O_TRUNC) {
        redis_.hset(key, "exists", "1");
        redis_.hset(key, "size", "0");
        redis_.hset(key, "mtime", now_ms());
    }
}

void CatalogManager::track_write(std::string_view filepath,
                                  uint64_t offset, uint64_t size) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "1");
    uint64_t end = offset + size;
    auto cur = redis_.hget(key, "size");
    uint64_t cur_size = 0;
    if (cur.has_value()) {
        try { cur_size = std::stoull(*cur); }
        catch (...) {}
    }
    if (end > cur_size) {
        redis_.hset(key, "size", std::to_string(end));
    }
    redis_.hset(key, "mtime", now_ms());
}

void CatalogManager::track_unlink(std::string_view filepath) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "0");
    redis_.hset(key, "size", "0");
    redis_.hset(key, "mtime", now_ms());
    redis_.del(location_key(filepath));
    redis_.del(offset_location_key(filepath));
}

void CatalogManager::track_truncate(std::string_view filepath,
                                     uint64_t new_size) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "1");
    redis_.hset(key, "size", std::to_string(new_size));
    redis_.hset(key, "mtime", now_ms());
    redis_.del(offset_location_key(filepath));
}

std::optional<FileInfo> CatalogManager::get_file_info(std::string_view filepath) {
    auto key = filemeta_key(filepath);
    auto exists_val = redis_.hget(key, "exists");
    if (!exists_val.has_value()) {
        return std::nullopt;
    }
    FileInfo info;
    info.exists = (*exists_val == "1");
    auto size_val = redis_.hget(key, "size");
    if (size_val.has_value()) {
        try { info.size = std::stoull(*size_val); }
        catch (...) {}
    }
    auto mtime_val = redis_.hget(key, "mtime");
    if (mtime_val.has_value()) {
        try { info.mtime_ms = std::stoull(*mtime_val); }
        catch (...) {}
    }
    return info;
}

} // namespace labios
