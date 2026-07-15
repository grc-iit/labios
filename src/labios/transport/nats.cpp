#include <labios/transport/nats.h>

#include <nats.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace labios::transport {

namespace {

struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
    size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
};

} // namespace

void NatsConnection::DurableAck::ack() {
    if (!message_ || acknowledged_.exchange(true)) return;
    if (natsMsg_Ack(static_cast<natsMsg*>(message_), nullptr) != NATS_OK) {
        acknowledged_.store(false);
        throw std::runtime_error("nats: durable ack failed");
    }
}

bool NatsConnection::DurableAck::acknowledged() const noexcept {
    return acknowledged_.load();
}

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
    jsCtx* js = nullptr;
    std::vector<natsSubscription*> subs;
    std::string stream_name = "LABIOS_LABELS";
    std::string stream_subject = "labios.>";
    std::mutex cb_mu;
    std::unordered_map<std::string, MessageCallback, StringHash, std::equal_to<>> callbacks;
    std::vector<std::shared_ptr<void>> durable_holders;

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
        if (js != nullptr) {
            jsCtx_Destroy(js);
        }
        if (conn != nullptr) {
            natsConnection_Drain(conn);
            natsConnection_Destroy(conn);
        }
    }

    void ensure_stream() {
        jsStreamConfig sc;
        jsStreamConfig_Init(&sc);
        const char* subjects[] = {"labios.labels", "labios.worker.*"};
        sc.Name = stream_name.c_str();
        sc.Subjects = subjects;
        sc.SubjectsLen = 2;
        sc.Retention = js_LimitsPolicy;
        sc.Storage = js_FileStorage;
        sc.MaxAge = 7LL * 24 * 60 * 60 * 1000000000LL;
        jsStreamInfo* info = nullptr;
        jsErrCode err{};
        auto s = js_AddStream(&info, js, &sc, nullptr, &err);
        if (info != nullptr) jsStreamInfo_Destroy(info);
        if (s != NATS_OK) {
            // A previous process may have created the stream. Updating with
            // the same contract is safe and makes startup idempotent.
            s = js_UpdateStream(nullptr, js, &sc, nullptr, &err);
        }
        if (s != NATS_OK) {
            throw std::runtime_error("nats: JetStream label stream setup failed");
        }
    }

    void ensure_inbox_sub() {
        if (inbox_sub != nullptr) return;

        natsInbox* inbox = nullptr;
        natsStatus is = natsInbox_Create(&inbox);
        if (is != NATS_OK || inbox == nullptr) {
            throw std::runtime_error("nats: inbox creation failed");
        }
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

        try {
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
        } catch (const std::exception& e) {
            fprintf(stderr, "[nats] inbox callback exception: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "[nats] inbox callback unknown exception\n");
        }
        natsMsg_Destroy(msg);
    }

    static void on_message(natsConnection* /*nc*/, natsSubscription* /*sub*/,
                           natsMsg* msg, void* closure) {
        auto* self = static_cast<Impl*>(closure);
        const char* subj = natsMsg_GetSubject(msg);
        std::string_view subject_sv(subj != nullptr ? subj : "");

        MessageCallback cb;
        {
            std::lock_guard lock(self->cb_mu);
            auto it = self->callbacks.find(subject_sv);
            if (it != self->callbacks.end()) {
                cb = it->second;
            }
        }

        if (cb) {
            const char* raw = natsMsg_GetData(msg);
            int len = natsMsg_GetDataLength(msg);
            auto span = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(raw),
                static_cast<size_t>(len));
            const char* reply = natsMsg_GetReply(msg);
            try {
                cb(subject_sv, span, reply != nullptr ? reply : "");
            } catch (const std::exception& e) {
                fprintf(stderr, "[nats] callback exception: %s\n", e.what());
            } catch (...) {
                fprintf(stderr, "[nats] callback unknown exception\n");
            }
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
    s = natsConnection_JetStream(&impl_->js, impl_->conn, nullptr);
    if (s != NATS_OK || impl_->js == nullptr) {
        throw std::runtime_error("nats: JetStream is required for durable delivery");
    }
    impl_->ensure_stream();
}

NatsConnection::~NatsConnection() = default;
NatsConnection::NatsConnection(NatsConnection&&) noexcept = default;
NatsConnection& NatsConnection::operator=(NatsConnection&&) noexcept = default;

// Stack-buffer null-terminator for short subjects (avoids heap allocation).
static const char* to_cstr(std::string_view sv, char* buf, size_t bufsz) {
    if (sv.size() < bufsz) {
        std::memcpy(buf, sv.data(), sv.size());
        buf[sv.size()] = '\0';
        return buf;
    }
    // Fallback for unexpectedly long subjects.
    static thread_local std::string overflow;
    overflow.assign(sv);
    return overflow.c_str();
}

void NatsConnection::publish(std::string_view subject,
                             std::span<const std::byte> data) {
    char subj_buf[64];
    natsStatus s = natsConnection_Publish(
        impl_->conn,
        to_cstr(subject, subj_buf, sizeof(subj_buf)),
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
    {
        std::lock_guard lock(impl_->cb_mu);
        impl_->callbacks[std::string(subject)] = std::move(callback);
    }
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

void NatsConnection::publish_durable(
    std::string_view subject, std::span<const std::byte> data) {
    jsPubAck* ack = nullptr;
    jsErrCode err{};
    auto s = js_Publish(&ack, impl_->js, std::string(subject).c_str(),
                        data.data(), static_cast<int>(data.size()), nullptr, &err);
    if (ack != nullptr) jsPubAck_Destroy(ack);
    if (s != NATS_OK) {
        throw std::runtime_error("nats: durable publish failed on " +
                                 std::string(subject));
    }
}

void NatsConnection::subscribe_durable(
    std::string_view subject, std::string_view durable,
    DurableCallback callback, int max_deliver,
    std::chrono::milliseconds ack_wait) {
    struct DurableClosure {
        Impl* impl;
        DurableCallback callback;
    };
    auto closure = std::make_shared<DurableClosure>(
        DurableClosure{impl_.get(), std::move(callback)});
    auto* raw_closure = closure.get();

    jsSubOptions opts;
    jsSubOptions_Init(&opts);
    opts.ManualAck = true;
    std::string durable_name(durable);
    std::string filter_subject(subject);
    opts.Config.Durable = durable_name.c_str();
    opts.Config.Name = durable_name.c_str();
    opts.Config.DeliverPolicy = js_DeliverAll;
    opts.Config.AckPolicy = js_AckExplicit;
    opts.Config.AckWait = ack_wait.count() * 1000000LL;
    opts.Config.MaxDeliver = max_deliver;
    opts.Config.FilterSubject = filter_subject.c_str();

    // The library owns the consumer configuration strings during this call.
    // Keep the closure alive for the subscription callback.
    natsSubscription* sub = nullptr;
    auto cb = [](natsConnection*, natsSubscription*, natsMsg* msg, void* data) {
        auto* c = static_cast<DurableClosure*>(data);
        const char* subj = natsMsg_GetSubject(msg);
        const char* raw = natsMsg_GetData(msg);
        int len = natsMsg_GetDataLength(msg);
        try {
            const char* reply = natsMsg_GetReply(msg);
            NatsConnection::DurableAck ack(static_cast<void*>(msg));
            c->callback(std::string_view(subj ? subj : ""),
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw),
                                            static_cast<size_t>(len)),
                std::string_view(reply ? reply : ""), ack);
        } catch (const std::exception& e) {
            fprintf(stderr, "[nats] durable callback exception: %s\\n", e.what());
        } catch (...) {
            fprintf(stderr, "[nats] durable callback exception\\n");
        }
        natsMsg_Destroy(msg);
    };
    jsErrCode err{};
    auto s = js_Subscribe(&sub, impl_->js, std::string(subject).c_str(), cb,
                          raw_closure, nullptr, &opts, &err);
    if (s != NATS_OK) {
        throw std::runtime_error("nats: durable subscribe failed on " +
                                 std::string(subject));
    }
    // Store the closure by using the callback map's lifetime anchor. The
    // subscription is destroyed before the connection, so release is safe.
    impl_->durable_holders.push_back(std::move(closure));
    impl_->subs.push_back(sub);
}

void NatsConnection::flush() {
    if (impl_->conn != nullptr) {
        natsStatus s = natsConnection_FlushTimeout(impl_->conn, 2000);
        if (s != NATS_OK && s != NATS_CONNECTION_CLOSED) {
            // Flush is called in catch blocks and shutdown paths where
            // throwing would be destructive. Log the error instead.
            fprintf(stderr, "nats: flush failed (status=%d)\n", static_cast<int>(s));
        }
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

    char subj_buf[64];
    natsStatus s;
    if (subject == "labios.labels") {
        natsMsg* msg = nullptr;
        s = natsMsg_Create(&msg, to_cstr(subject, subj_buf, sizeof(subj_buf)),
                           reply_to.c_str(),
                           reinterpret_cast<const char*>(data.data()),
                           static_cast<int>(data.size()));
        if (s == NATS_OK) {
            jsPubAck* ack = nullptr;
            jsErrCode err{};
            s = js_PublishMsg(&ack, impl_->js, msg, nullptr, &err);
            if (ack != nullptr) jsPubAck_Destroy(ack);
            if (s != NATS_OK) natsMsg_Destroy(msg);
        }
    } else {
        s = natsConnection_PublishRequest(
            impl_->conn,
            to_cstr(subject, subj_buf, sizeof(subj_buf)),
            reply_to.c_str(),
            reinterpret_cast<const void*>(data.data()),
            static_cast<int>(data.size()));
    }
    if (s != NATS_OK) {
        std::lock_guard lock(impl_->reply_mu);
        impl_->pending_replies.erase(reply_to);
        throw std::runtime_error("nats: async publish failed on " + std::string(subject));
    }
    return reply;
}

std::pair<std::string, std::shared_ptr<AsyncReply>>
NatsConnection::create_reply_inbox() {
    impl_->ensure_inbox_sub();

    uint64_t seq = impl_->inbox_counter.fetch_add(1);
    std::string reply_to = impl_->inbox_prefix + "." + std::to_string(seq);

    auto reply = std::make_shared<AsyncReply>();
    {
        std::lock_guard lock(impl_->reply_mu);
        impl_->pending_replies[reply_to] = reply;
    }
    return {reply_to, reply};
}

bool NatsConnection::connected() const {
    return impl_ && impl_->conn != nullptr &&
           natsConnection_Status(impl_->conn) == NATS_CONN_STATUS_CONNECTED;
}

} // namespace labios::transport
