#include <labios/transport/nats.h>

#include <nats.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace labios::transport {

struct NatsConnection::Impl {
    natsConnection* conn = nullptr;
    std::vector<natsSubscription*> subs;
    MessageCallback cb;

    ~Impl() {
        for (auto* sub : subs) {
            natsSubscription_Drain(sub);
            natsSubscription_Destroy(sub);
        }
        if (conn != nullptr) {
            natsConnection_Drain(conn);
            natsConnection_Destroy(conn);
        }
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
            self->cb(subj != nullptr ? subj : "", span);
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

bool NatsConnection::connected() const {
    return impl_ && impl_->conn != nullptr &&
           natsConnection_Status(impl_->conn) == NATS_CONN_STATUS_CONNECTED;
}

} // namespace labios::transport
