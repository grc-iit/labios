#include <labios/content_manager.h>

#include <algorithm>
#include <cstring>

namespace labios {

ReadPolicy read_policy_from_string(std::string_view s) {
    if (s == "write-only") return ReadPolicy::WriteOnly;
    return ReadPolicy::ReadThrough;
}

ContentManager::ContentManager(transport::RedisConnection& redis,
                               uint64_t min_label_size,
                               int flush_interval_ms,
                               ReadPolicy default_read_policy)
    : redis_(redis),
      min_label_size_(min_label_size),
      flush_interval_ms_(flush_interval_ms),
      default_read_policy_(default_read_policy) {}

ContentManager::~ContentManager() {
    // Drain any buffered small-I/O writes before shutting down.
    auto regions = flush_all();
    if (flush_callback_) {
        for (auto& [fd, fd_regions] : regions) {
            flush_callback_(fd, std::move(fd_regions));
        }
    }

    if (flush_thread_.joinable()) {
        flush_thread_.request_stop();
    }
}

std::string ContentManager::data_key(uint64_t label_id, uint32_t app_id,
                                      std::string_view room_id) {
    if (!room_id.empty()) {
        return "labios:room:" + std::string(room_id) + ":" + std::to_string(label_id);
    }
    if (app_id != 0) {
        return "labios:data:" + std::to_string(app_id) + ":" + std::to_string(label_id);
    }
    return "labios:data:" + std::to_string(label_id);
}

void ContentManager::stage(uint64_t label_id, std::span<const std::byte> data,
                            uint32_t app_id, std::string_view room_id) {
    redis_.set_binary(data_key(label_id, app_id, room_id), data);
}

std::vector<std::byte> ContentManager::retrieve(uint64_t label_id,
                                                  uint32_t app_id,
                                                  std::string_view room_id) {
    return redis_.get_binary(data_key(label_id, app_id, room_id));
}

void ContentManager::remove(uint64_t label_id, uint32_t app_id,
                              std::string_view room_id) {
    redis_.del(data_key(label_id, app_id, room_id));
}

bool ContentManager::exists(uint64_t label_id, uint32_t app_id,
                              std::string_view room_id) {
    return redis_.get(data_key(label_id, app_id, room_id)).has_value();
}

std::vector<FlushRegion> ContentManager::cache_write(
    int fd, std::string_view filepath, uint64_t offset,
    std::span<const std::byte> data) {

    std::unique_lock lock(cache_mu_);
    auto& cache = caches_[fd];
    if (cache.filepath.empty()) {
        cache.filepath = std::string(filepath);
        cache.read_policy = default_read_policy_;
    }

    auto it = cache.regions.find(offset);
    if (it != cache.regions.end()) {
        cache.total_bytes -= it->second.size();
    }
    cache.regions[offset].assign(data.begin(), data.end());
    cache.total_bytes += data.size();

    if (cache.total_bytes >= min_label_size_) {
        return flush_locked(cache);
    }
    return {};
}

std::optional<std::vector<std::byte>> ContentManager::cache_read(
    int fd, uint64_t offset, uint64_t size) {

    std::shared_lock lock(cache_mu_);
    auto it = caches_.find(fd);
    if (it == caches_.end()) return std::nullopt;
    if (it->second.read_policy == ReadPolicy::WriteOnly) return std::nullopt;

    auto& regions = it->second.regions;
    uint64_t req_end = offset + size;

    // Track which bytes of the requested range are covered by cached regions.
    std::vector<std::byte> result(size, std::byte{0});
    uint64_t covered = 0;

    // Iterate regions that could overlap [offset, offset+size).
    // regions is a std::map<uint64_t, vector<byte>> sorted by offset.
    // Start at the first region whose offset is <= offset (it might cover us).
    auto rit = regions.upper_bound(offset);
    if (rit != regions.begin()) --rit;

    for (; rit != regions.end(); ++rit) {
        uint64_t reg_start = rit->first;
        uint64_t reg_end = reg_start + rit->second.size();

        if (reg_start >= req_end) break;          // past our range
        if (reg_end <= offset) { continue; }       // before our range

        // Compute the overlap [ov_start, ov_end)
        uint64_t ov_start = std::max(reg_start, offset);
        uint64_t ov_end = std::min(reg_end, req_end);
        uint64_t ov_len = ov_end - ov_start;

        std::memcpy(result.data() + (ov_start - offset),
                     rit->second.data() + (ov_start - reg_start),
                     ov_len);
        covered += ov_len;
    }

    if (covered == 0) return std::nullopt;
    if (covered < size) return std::nullopt;  // partial hit: let caller issue READ labels
    return result;
}

std::vector<FlushRegion> ContentManager::flush(int fd) {
    std::unique_lock lock(cache_mu_);
    auto it = caches_.find(fd);
    if (it == caches_.end()) return {};
    return flush_locked(it->second);
}

std::vector<std::pair<int, std::vector<FlushRegion>>>
ContentManager::flush_all() {
    std::unique_lock lock(cache_mu_);
    std::vector<std::pair<int, std::vector<FlushRegion>>> result;
    for (auto& [fd, cache] : caches_) {
        if (cache.total_bytes > 0) {
            result.emplace_back(fd, flush_locked(cache));
        }
    }
    return result;
}

void ContentManager::evict(int fd) {
    std::unique_lock lock(cache_mu_);
    caches_.erase(fd);
}

void ContentManager::set_read_policy(int fd, ReadPolicy policy) {
    std::unique_lock lock(cache_mu_);
    caches_[fd].read_policy = policy;
}

std::vector<FlushRegion> ContentManager::flush_locked(FdCache& cache) {
    std::vector<FlushRegion> result;
    for (auto& [offset, data] : cache.regions) {
        result.push_back({cache.filepath, offset, std::move(data)});
    }
    cache.regions.clear();
    cache.total_bytes = 0;
    return result;
}

void ContentManager::set_flush_callback(FlushCallback cb) {
    flush_callback_ = std::move(cb);
}

void ContentManager::start_flush_timer() {
    if (flush_interval_ms_ <= 0) return;

    flush_thread_ = std::jthread([this](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(flush_interval_ms_));
            if (stoken.stop_requested()) break;

            auto flushed = flush_all();
            if (flush_callback_) {
                for (auto& [fd, regions] : flushed) {
                    flush_callback_(fd, std::move(regions));
                }
            }
        }
    });
}

} // namespace labios
