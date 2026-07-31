#pragma once

#include <labios/client.h>
#include <labios/session.h>

#include <memory>

namespace labios::detail {

class OperationContext {
public:
    virtual ~OperationContext() = default;
    virtual CompletionResult test(uint64_t label_id) const = 0;
    virtual WaitResult wait_any(std::span<const uint64_t> label_ids,
                                std::chrono::milliseconds timeout) const = 0;
    virtual WaitResult wait_all(std::span<const uint64_t> label_ids,
                                std::chrono::milliseconds timeout) const = 0;
    virtual CancellationResult cancel(uint64_t label_id) const = 0;
    virtual std::vector<std::byte> read(
        std::span<const uint64_t> label_ids,
        std::chrono::milliseconds timeout) const = 0;
    virtual std::chrono::milliseconds default_timeout() const = 0;
};

class SessionOperationContext final : public OperationContext {
public:
    explicit SessionOperationContext(std::shared_ptr<Session> session)
        : session_(std::move(session)) {}

    CompletionResult test(uint64_t label_id) const override {
        return session_->label_manager().test(label_id);
    }
    WaitResult wait_any(std::span<const uint64_t> label_ids,
                        std::chrono::milliseconds timeout) const override {
        return session_->label_manager().wait_any(label_ids, timeout);
    }
    WaitResult wait_all(std::span<const uint64_t> label_ids,
                        std::chrono::milliseconds timeout) const override {
        return session_->label_manager().wait_all(label_ids, timeout);
    }
    CancellationResult cancel(uint64_t label_id) const override {
        return session_->label_manager().cancel(label_id);
    }
    std::vector<std::byte> read(
        std::span<const uint64_t> label_ids,
        std::chrono::milliseconds timeout) const override {
        return session_->label_manager().wait_read(label_ids, timeout);
    }
    std::chrono::milliseconds default_timeout() const override {
        return std::chrono::milliseconds(session_->config().reply_timeout_ms);
    }

private:
    std::shared_ptr<Session> session_;
};

class OperationFactory {
public:
    static Operation create(std::shared_ptr<OperationContext> context,
                            std::vector<uint64_t> label_ids,
                            OperationKind kind) {
        return Operation(std::move(context), std::move(label_ids), kind);
    }
};

} // namespace labios::detail
