#include <labios/transport/nats.h>

#include <nats.h>

#include <atomic>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace labios::transport {

std::vector<std::byte> AsyncReply::wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mu);
    cv.wait_for(lock, timeout, [this] { return completed; });
    if (!completed) {
        throw std::runtime_error("nats: async reply timed out");
    }
    return std::move(data);
}

struct NatsConnection::Impl {
    natsConnection* conn = nullptr;
    std::vector<natsSubscription*> subs;
    MessageCallback cb;

    // Async reply infrastructure: a wildcard inbox subscription that
    // routes incoming replies to the correct AsyncReply handle.
    std::string inbox_prefix;
    natsSubscription* inbox_sub = nullptr;
    std::atomic<uint64_t> inbox_counter{0};
    std::mutex reply_mu;
    std::unordered_map<std::string, std::shared_ptr<AsyncReply>> pending_replies;

    ~Impl() {
        if (inbox_sub != nullptr) {
            natsSubscription_Drain(inbox_sub);
            natsSubscription_Destroy(inbox_sub);
        }
        for (auto* sub : subs) {
            natsSubscription_Drain(sub);
            natsSubscription_Destroy(sub);
        }
        if (conn != nullptr) {
            natsConnection_Drain(conn);
            natsConnection_Destroy(conn);
        }
    }

    void ensure_inbox_sub() {
        if (inbox_sub != nullptr) return;

        natsInbox* inbox = nullptr;
        natsInbox_Create(&inbox);
        inbox_prefix = std::string(inbox);
        natsInbox_Destroy(inbox);
        // Remove trailing dot if present, then add ".*" for wildcard
        if (!inbox_prefix.empty() && inbox_prefix.back() == '.') {
            inbox_prefix.pop_back();
        }
        std::string wildcard = inbox_prefix + ".*";

        natsStatus s = natsConnection_Subscribe(
            &inbox_sub, conn, wildcard.c_str(),
            on_inbox_message, this);
        if (s != NATS_OK) {
            throw std::runtime_error("nats: inbox subscription failed");
        }
    }

    static void on_inbox_message(natsConnection* /*nc*/,
                                  natsSubscription* /*sub*/,
                                  natsMsg* msg, void* closure) {
        auto* self = static_cast<Impl*>(closure);
        const char* subj = natsMsg_GetSubject(msg);
        const char* raw = natsMsg_GetData(msg);
        int len = natsMsg_GetDataLength(msg);

        if (subj) {
            std::shared_ptr<AsyncReply> reply;
            {
                std::lock_guard lock(self->reply_mu);
                auto it = self->pending_replies.find(subj);
                if (it != self->pending_replies.end()) {
                    reply = it->second;
                    self->pending_replies.erase(it);
                }
            }
            if (reply) {
                std::lock_guard lock(reply->mu);
                if (raw && len > 0) {
                    auto* begin = reinterpret_cast<const std::byte*>(raw);
                    reply->data.assign(begin, begin + len);
                }
                reply->completed = true;
                reply->cv.notify_one();
            }
        }
        natsMsg_Destroy(msg);
    }

    static void on_message(natsConnection* /*nc*/, natsSubscription* /*sub*/,
                           natsMsg* msg, void* closure) {
        auto* self = static_cast<Impl*>(closure);
        if (self->cb) {
            const char* subj = natsMsg_GetSubject(msg);
            const char* raw = natsMsg_GetData(msg);
            int len = natsMsg_GetDataLength(msg);
            auto span = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(raw),
                static_cast<size_t>(len));
            const char* reply = natsMsg_GetReply(msg);
            self->cb(subj != nullptr ? subj : "", span,
                     reply != nullptr ? reply : "");
        }
        natsMsg_Destroy(msg);
    }
};

NatsConnection::NatsConnection(std::string_view url)
    : impl_(std::make_unique<Impl>()) {
    natsOptions* opts = nullptr;
    natsStatus s = natsOptions_Create(&opts);
    if (s != NATS_OK) {
        throw std::runtime_error("nats: failed to create options");
    }
    natsOptions_SetURL(opts, std::string(url).c_str());
    natsOptions_SetRetryOnFailedConnect(opts, true, nullptr, nullptr);
    natsOptions_SetMaxReconnect(opts, 10);
    natsOptions_SetReconnectWait(opts, 500);

    s = natsConnection_Connect(&impl_->conn, opts);
    natsOptions_Destroy(opts);
    if (s != NATS_OK) {
        throw std::runtime_error("nats: connection failed to " + std::string(url));
    }
}

NatsConnection::~NatsConnection() = default;
NatsConnection::NatsConnection(NatsConnection&&) noexcept = default;
NatsConnection& NatsConnection::operator=(NatsConnection&&) noexcept = default;

void NatsConnection::publish(std::string_view subject,
                             std::span<const std::byte> data) {
    natsStatus s = natsConnection_Publish(
        impl_->conn,
        std::string(subject).c_str(),
        reinterpret_cast<const void*>(data.data()),
        static_cast<int>(data.size()));
    if (s != NATS_OK) {
        throw std::runtime_error("nats: publish failed on " + std::string(subject));
    }
}

void NatsConnection::publish(std::string_view subject, std::string_view data) {
    auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.data()), data.size());
    publish(subject, span);
}

void NatsConnection::subscribe(std::string_view subject,
                               MessageCallback callback) {
    impl_->cb = std::move(callback);
    natsSubscription* sub = nullptr;
    natsStatus s = natsConnection_Subscribe(
        &sub, impl_->conn,
        std::string(subject).c_str(),
        Impl::on_message, impl_.get());
    if (s != NATS_OK) {
        throw std::runtime_error("nats: subscribe failed on " + std::string(subject));
    }
    impl_->subs.push_back(sub);
}

void NatsConnection::flush() {
    if (impl_->conn != nullptr) {
        natsConnection_FlushTimeout(impl_->conn, 2000);
    }
}

NatsConnection::Reply NatsConnection::request(
    std::string_view subject,
    std::span<const std::byte> data,
    std::chrono::milliseconds timeout) {
    natsMsg* reply_msg = nullptr;
    natsStatus s = natsConnection_Request(
        &reply_msg, impl_->conn,
        std::string(subject).c_str(),
        reinterpret_cast<const void*>(data.data()),
        static_cast<int>(data.size()),
        static_cast<int64_t>(timeout.count()));
    if (s != NATS_OK) {
        throw std::runtime_error("nats: request failed on " + std::string(subject));
    }
    Reply result;
    const char* rdata = natsMsg_GetData(reply_msg);
    int rlen = natsMsg_GetDataLength(reply_msg);
    if (rdata != nullptr && rlen > 0) {
        auto* begin = reinterpret_cast<const std::byte*>(rdata);
        result.data.assign(begin, begin + rlen);
    }
    natsMsg_Destroy(reply_msg);
    return result;
}

std::shared_ptr<AsyncReply> NatsConnection::publish_request_async(
    std::string_view subject, std::span<const std::byte> data) {
    impl_->ensure_inbox_sub();

    uint64_t seq = impl_->inbox_counter.fetch_add(1);
    std::string reply_to = impl_->inbox_prefix + "." + std::to_string(seq);

    auto reply = std::make_shared<AsyncReply>();
    {
        std::lock_guard lock(impl_->reply_mu);
        impl_->pending_replies[reply_to] = reply;
    }

    natsStatus s = natsConnection_PublishRequest(
        impl_->conn,
        std::string(subject).c_str(),
        reply_to.c_str(),
        reinterpret_cast<const void*>(data.data()),
        static_cast<int>(data.size()));
    if (s != NATS_OK) {
        std::lock_guard lock(impl_->reply_mu);
        impl_->pending_replies.erase(reply_to);
        throw std::runtime_error("nats: async publish failed on " + std::string(subject));
    }
    return reply;
}

bool NatsConnection::connected() const {
    return impl_ && impl_->conn != nullptr &&
           natsConnection_Status(impl_->conn) == NATS_CONN_STATUS_CONNECTED;
}

} // namespace labios::transport
