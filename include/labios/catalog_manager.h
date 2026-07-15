#pragma once

#include <labios/label.h>
#include <labios/transport/redis.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

enum class LabelStatus : uint8_t {
    Queued, Parked, Scheduled, Executing, Complete, Error, Cancelled
};

struct DependencyReadiness {
    bool ready = true;
    std::string reason;
};

struct ScheduleEntry {
    uint64_t label_id;
    int worker_id;
    uint32_t flags;
};

std::string to_string(LabelStatus status);
LabelStatus label_status_from_string(std::string_view s);
uint64_t parking_backoff_ms(uint64_t attempt) noexcept;

struct FileInfo {
    uint64_t size = 0;
    uint64_t mtime_ms = 0;
    bool exists = false;
};

class CatalogManager {
public:
    explicit CatalogManager(transport::RedisConnection& redis);

    void create(uint64_t label_id, uint32_t app_id, LabelType type);
    void create(const LabelData& label);
    /// Persist the canonical label snapshot before transport handoff.
    /// Atomic admission transition: canonical snapshot and queued recovery
    /// metadata become visible in one catalog hash update.
    void admit(const LabelData& label);
    /// Commit dispatcher ingress only while the record is new/queued. Returns
    /// false when a redelivery observes a later lifecycle state.
    bool durable_handoff(const LabelData& label);
    void persist_snapshot(const LabelData& label);
    std::optional<LabelData> get_snapshot(uint64_t label_id);
    void persist_composite(const LabelData& parent,
                           std::span<const std::vector<std::byte>> children);
    std::vector<std::vector<std::byte>> get_composite_program(uint64_t parent_id);
    std::vector<LabelData> recoverable_labels(uint64_t now_ms);
    size_t wake_parked(uint64_t now_ms);
    void park(const LabelData& label, std::string_view reason,
              uint64_t attempt, uint64_t next_retry_ms,
              std::string_view last_error = {});
    uint64_t park_attempts(uint64_t label_id);
    DependencyReadiness dependency_readiness(const LabelData& label) noexcept;
    void set_composite_parent(uint64_t child_id, uint64_t composite_id);
    void set_status(uint64_t label_id, LabelStatus status);
    /// Compare-and-set used by the pre-execution cancellation boundary.
    bool cancel_if_pre_execution(uint64_t label_id);
    LabelStatus get_status(uint64_t label_id);
    void set_completion(const CompletionData& completion);
    std::optional<CompletionData> get_completion(uint64_t label_id);
    void set_flags(uint64_t label_id, uint32_t flags);
    uint32_t get_flags(uint64_t label_id);
    void set_error(uint64_t label_id, std::string_view error);
    std::optional<std::string> get_error(uint64_t label_id);
    void set_worker(uint64_t label_id, int worker_id);
    std::optional<int> get_worker(uint64_t label_id);
    void schedule_batch(std::span<const ScheduleEntry> entries);

    /// Track which worker holds data for a given filepath (whole-file, legacy).
    void set_location(std::string_view filepath, int worker_id);
    std::optional<int> get_location(std::string_view filepath);

    /// Per-offset location tracking: records which worker holds a specific
    /// byte range of a file, enabling correct read-locality when a single
    /// file is split across multiple workers.
    void set_location(std::string_view filepath, uint64_t offset,
                      uint64_t length, int worker_id);
    std::optional<int> get_location(std::string_view filepath,
                                    uint64_t offset, uint64_t length);

    void track_open(std::string_view filepath, int flags);
    void track_write(std::string_view filepath, uint64_t offset, uint64_t size);
    void track_unlink(std::string_view filepath);
    void track_truncate(std::string_view filepath, uint64_t new_size);
    std::optional<FileInfo> get_file_info(std::string_view filepath);

private:
    transport::RedisConnection& redis_;
    std::string catalog_key(uint64_t label_id) const;
    static std::string location_key(std::string_view filepath);
    static std::string offset_location_key(std::string_view filepath);
    static std::string filemeta_key(std::string_view filepath);
};

} // namespace labios
