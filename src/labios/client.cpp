#include <labios/client.h>
#include <labios/session.h>

#include <stdexcept>

namespace labios {

Client::Client(const Config& cfg) : session_(std::make_unique<Session>(cfg)) {}
Client::~Client() = default;

void Client::write(std::string_view filepath, std::span<const std::byte> data,
                   uint64_t offset) {
    auto& label_mgr = session_->label_manager();
    auto& catalog_mgr = session_->catalog_manager();

    // The native API always publishes directly via LabelManager.
    // The small-I/O cache is for the POSIX intercept where an fd
    // corresponds to a specific file with accumulated writes.
    auto pending = label_mgr.publish_write(filepath, offset, data);
    label_mgr.wait(pending);
    catalog_mgr.track_write(filepath, offset, data.size());
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                    uint64_t offset, uint64_t size) {
    auto& label_mgr = session_->label_manager();

    // The native API always reads via labels. The small-I/O cache
    // is only used by the POSIX intercept.
    auto pending = label_mgr.publish_read(filepath, offset, size);
    return label_mgr.wait_read(pending);
}

Session& Client::session() { return *session_; }

Client connect(const Config& cfg) { return Client(cfg); }

} // namespace labios
