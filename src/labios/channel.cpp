#include <labios/channel.h>

#include <charconv>
#include <cstring>
#include <stdexcept>

namespace labios {

namespace {

bool parse_u64(std::string_view input, uint64_t& value) {
    auto* begin = input.data();
    auto* end = begin + input.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    return ec == std::errc{} && ptr == end;
}

bool parse_notification(std::span<const std::byte> payload,
                        uint64_t& sequence,
                        uint64_t& label_id) {
    auto raw = std::string_view(
        reinterpret_cast<const char*>(payload.data()), payload.size());
    auto first_colon = raw.find(':');
    if (first_colon == std::string_view::npos) {
        return false;
    }

    auto second_colon = raw.find(':', first_colon + 1);
    if (second_colon == std::string_view::npos) {
        return false;
    }

    return parse_u64(raw.substr(0, first_colon), sequence)
        && parse_u64(raw.substr(first_colon + 1,
                                second_colon - first_colon - 1), label_id);
}

} // namespace

// ---------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------

Channel::Channel(std::string name,
                 transport::RedisConnection& redis,
                 transport::NatsConnection& nats,
                 uint32_t ttl_seconds,
                 uint64_t max_pending)
    : name_(std::move(name))
    , redis_(redis)
    , nats_(nats)
    , ttl_seconds_(ttl_seconds)
    , max_pending_(max_pending) {}

std::string Channel::nats_subject() const {
    return "labios.channel." + name_;
}

std::string Channel::warehouse_key(uint64_t seq) const {
    return "labios:channel:" + name_ + ":" + std::to_string(seq);
}

std::string Channel::warehouse_pattern() const {
    return "labios:channel:" + name_ + ":*";
}

uint64_t Channel::publish(std::span<const std::byte> data, uint64_t label_id) {
    if (draining_.load() || destroyed_.load()) {
        return 0;
    }

    // Backpressure: reject if pending messages exceed configurable limit
    if (max_pending_ > 0 && (next_seq_.load() - 1) >= max_pending_) {
        return 0;
    }

    uint64_t seq = next_seq_.fetch_add(1);

    // Stage data in warehouse
    auto key = warehouse_key(seq);
    redis_.set_binary(key, data);

    if (ttl_seconds_ > 0) {
        redis_.expire(key, ttl_seconds_);
    }

    // Build notification payload: "seq:label_id:data_size"
    auto notification = std::to_string(seq) + ":"
                      + std::to_string(label_id) + ":"
                      + std::to_string(data.size());
    nats_.publish(nats_subject(), notification);
    nats_.flush();

    return seq;
}

int Channel::subscribe(ChannelCallback cb) {
    if (destroyed_.load()) {
        return -1;
    }

    int sub_id;
    {
        std::lock_guard lock(mu_);
        sub_id = next_sub_id_++;
        subscribers_[sub_id] = std::move(cb);
    }

    // Only set up the NATS subscription on the first subscriber.
    // NatsConnection::subscribe replaces the callback for a given subject,
    // so calling it multiple times is safe but unnecessary.
    if (subscriber_count() == 1) {
        nats_.subscribe(nats_subject(),
            [this](std::string_view /*subject*/,
                   std::span<const std::byte> payload,
                   std::string_view /*reply_to*/) {
                if (destroyed_.load()) return;

                // Parse notification: "seq:label_id:data_size"
                uint64_t seq = 0;
                uint64_t label_id = 0;
                if (!parse_notification(payload, seq, label_id)) {
                    return;
                }

                // Retrieve data from warehouse
                auto data = redis_.get_binary(warehouse_key(seq));

                ChannelMessage cm;
                cm.sequence = seq;
                cm.label_id = label_id;
                cm.data = std::move(data);

                std::vector<ChannelCallback> callbacks;
                {
                    std::shared_lock lock(mu_);
                    callbacks.reserve(subscribers_.size());
                    for (const auto& [id, callback] : subscribers_) {
                        (void)id;
                        callbacks.push_back(callback);
                    }
                }

                for (auto& callback : callbacks) {
                    callback(cm);
                }
            });
    }

    return sub_id;
}

void Channel::unsubscribe(int sub_id) {
    bool should_destroy = false;
    {
        std::lock_guard lock(mu_);
        subscribers_.erase(sub_id);
        if (subscribers_.empty() && draining_.load()) {
            should_destroy = true;
        }
    }
    if (should_destroy) {
        destroy();
    }
}

int Channel::subscriber_count() const {
    std::shared_lock lock(mu_);
    return static_cast<int>(subscribers_.size());
}

void Channel::drain() {
    draining_.store(true);

    // If no subscribers, destroy immediately
    bool empty;
    {
        std::shared_lock lock(mu_);
        empty = subscribers_.empty();
    }
    if (empty) {
        destroy();
    }
}

void Channel::destroy() {
    if (destroyed_.exchange(true)) {
        return; // Already destroyed
    }

    {
        std::lock_guard lock(mu_);
        subscribers_.clear();
    }

    cleanup_warehouse();
}

void Channel::cleanup_warehouse() {
    auto keys = redis_.scan_keys(warehouse_pattern());
    for (const auto& key : keys) {
        redis_.del(key);
    }
}

// ---------------------------------------------------------------------------
// ChannelRegistry
// ---------------------------------------------------------------------------

ChannelRegistry::ChannelRegistry(transport::RedisConnection& redis,
                                 transport::NatsConnection& nats)
    : redis_(redis), nats_(nats) {}

Channel* ChannelRegistry::create(std::string_view name, uint32_t ttl_seconds) {
    std::lock_guard lock(mu_);
    auto key = std::string(name);
    if (channels_.find(key) != channels_.end()) {
        return nullptr;
    }
    auto ch = std::make_unique<Channel>(key, redis_, nats_, ttl_seconds);
    auto* ptr = ch.get();
    channels_[key] = std::move(ch);
    return ptr;
}

Channel* ChannelRegistry::get(std::string_view name) {
    std::shared_lock lock(mu_);
    auto it = channels_.find(name);
    if (it == channels_.end()) {
        return nullptr;
    }
    return it->second.get();
}

void ChannelRegistry::remove(std::string_view name) {
    std::lock_guard lock(mu_);
    auto it = channels_.find(name);
    if (it != channels_.end()) {
        channels_.erase(it);
    }
}

std::vector<std::string> ChannelRegistry::list() const {
    std::shared_lock lock(mu_);
    std::vector<std::string> names;
    names.reserve(channels_.size());
    for (const auto& [name, _] : channels_) {
        names.push_back(name);
    }
    return names;
}

} // namespace labios
