#pragma once

#include <labios/catalog_manager.h>
#include <labios/content_manager.h>
#include <labios/label.h>
#include <labios/transport/nats.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace labios {

struct PendingLabel {
    uint64_t label_id = 0;
    std::vector<std::byte> reply_data;
    std::shared_ptr<transport::AsyncReply> async_reply;
};

class LabelManager {
public:
    LabelManager(ContentManager& content, CatalogManager& catalog,
                 transport::NatsConnection& nats,
                 uint64_t max_label_size, uint32_t app_id);

    std::vector<PendingLabel> publish_write(
        std::string_view filepath, uint64_t offset,
        std::span<const std::byte> data);

    std::vector<PendingLabel> publish_read(
        std::string_view filepath, uint64_t offset, uint64_t size);

    void wait(std::span<PendingLabel> pending);

    std::vector<std::byte> wait_read(std::span<PendingLabel> pending);

    uint64_t label_count(uint64_t data_size) const;

private:
    ContentManager& content_;
    CatalogManager& catalog_;
    transport::NatsConnection& nats_;
    uint64_t max_label_size_;
    uint32_t app_id_;
};

} // namespace labios
