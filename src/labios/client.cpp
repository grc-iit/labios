#include <labios/client.h>
#include <labios/session.h>

#include <stdexcept>

namespace labios {

Client::Client(const Config& cfg) : session_(std::make_unique<Session>(cfg)) {}
Client::~Client() = default;

void Client::write(std::string_view filepath, std::span<const std::byte> data,
                   uint64_t offset) {
    auto& cfg = session_->config();
    auto& label_mgr = session_->label_manager();
    auto& content_mgr = session_->content_manager();
    auto& catalog_mgr = session_->catalog_manager();

    if (data.size() < cfg.label_min_size) {
        // Small write goes to cache
        auto flush_regions = content_mgr.cache_write(
            -1, filepath, offset, data);
        for (auto& region : flush_regions) {
            auto pending = label_mgr.publish_write(
                region.filepath, region.offset, region.data);
            label_mgr.wait(pending);
            catalog_mgr.track_write(region.filepath, region.offset,
                                     region.data.size());
        }
    } else {
        // Normal/large write: split into labels
        auto pending = label_mgr.publish_write(filepath, offset, data);
        label_mgr.wait(pending);
        catalog_mgr.track_write(filepath, offset, data.size());
    }
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                    uint64_t offset, uint64_t size) {
    auto& label_mgr = session_->label_manager();
    auto& content_mgr = session_->content_manager();

    // Check cache first (read-through)
    auto cached = content_mgr.cache_read(-1, offset, size);
    if (cached.has_value() && cached->size() == size) {
        return *cached;
    }

    // Issue READ labels
    auto pending = label_mgr.publish_read(filepath, offset, size);
    return label_mgr.wait_read(pending);
}

Session& Client::session() { return *session_; }

Client connect(const Config& cfg) { return Client(cfg); }

} // namespace labios
