#include "../../src/labios/operation_context.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {

class FakeContext final : public labios::detail::OperationContext {
public:
    labios::CompletionResult test(uint64_t id) const override {
        std::lock_guard lock(mutex_);
        auto it = states_.find(id);
        if (it == states_.end()) {
            return {id, labios::CompletionState::Unknown, "LOOKUP_FAILED",
                    "unknown test label", {}};
        }
        return it->second;
    }

    labios::WaitResult wait_any(
        std::span<const uint64_t> ids,
        std::chrono::milliseconds timeout) const override {
        for (const auto id : ids) {
            auto result = test(id);
            if (result.terminal()) return {result.state, {std::move(result)}};
        }
        if (timeout.count() >= 0) return {labios::CompletionState::Timeout, {}};
        return {};
    }

    labios::WaitResult wait_all(
        std::span<const uint64_t> ids,
        std::chrono::milliseconds timeout) const override {
        labios::WaitResult result{labios::CompletionState::Complete, {}};
        for (const auto id : ids) {
            auto item = test(id);
            if (!item.terminal()) result.state = labios::CompletionState::Timeout;
            result.results.push_back(std::move(item));
        }
        (void)timeout;
        return result;
    }

    labios::CancellationResult cancel(uint64_t id) const override {
        std::lock_guard lock(mutex_);
        auto it = states_.find(id);
        if (it == states_.end()) {
            return {id, labios::CancellationState::Unknown,
                    {id, labios::CompletionState::Unknown, "LOOKUP_FAILED",
                     "unknown test label", {}}};
        }
        if (it->second.terminal()) {
            return {id, it->second.state == labios::CompletionState::Cancelled
                            ? labios::CancellationState::Cancelled
                            : labios::CancellationState::Terminal,
                    it->second};
        }
        it->second.state = labios::CompletionState::Cancelled;
        it->second.category = "CANCELED";
        it->second.error = "cancelled by test";
        return {id, labios::CancellationState::Cancelled, it->second};
    }

    std::vector<std::byte> read(
        std::span<const uint64_t> ids,
        std::chrono::milliseconds timeout) const override {
        auto result = wait_all(ids, timeout);
        if (result.state == labios::CompletionState::Timeout) {
            throw labios::CompletionError(labios::CompletionState::Timeout,
                                          ids.empty() ? 0 : ids.front(),
                                          "test timeout; operation remains active");
        }
        return bytes_;
    }

    std::chrono::milliseconds default_timeout() const override {
        return std::chrono::milliseconds(1);
    }

    void set(uint64_t id, labios::CompletionState state,
             uint64_t completed_us = 0) {
        std::lock_guard lock(mutex_);
        states_[id] = {id, state, {}, {}, {}};
        states_[id].completed_us = completed_us;
    }

    void set_bytes(std::vector<std::byte> bytes) { bytes_ = std::move(bytes); }

private:
    mutable std::mutex mutex_;
    mutable std::unordered_map<uint64_t, labios::CompletionResult> states_;
    std::vector<std::byte> bytes_;
};

labios::Operation operation(const std::shared_ptr<FakeContext>& context,
                            std::initializer_list<uint64_t> ids,
                            labios::OperationKind kind = labios::OperationKind::Generic) {
    return labios::detail::OperationFactory::create(
        context, std::vector<uint64_t>(ids), kind);
}

} // namespace

TEST_CASE("Operation owns its context after the producer is destroyed",
          "[api-lifetime]") {
    auto context = std::make_shared<FakeContext>();
    context->set(41, labios::CompletionState::Complete, 100);
    std::weak_ptr<FakeContext> lifetime = context;
    auto handle = operation(context, {41});
    context.reset();

    REQUIRE_FALSE(lifetime.expired());
    CHECK(handle.test().state == labios::CompletionState::Complete);
    handle = {};
    CHECK(lifetime.expired());
}

TEST_CASE("Timeout is nonterminal and an Operation remains reusable",
          "[api-lifetime]") {
    auto context = std::make_shared<FakeContext>();
    context->set(7, labios::CompletionState::Pending);
    auto handle = operation(context, {7});

    const auto first = handle.wait_for(std::chrono::milliseconds(0));
    CHECK(first.state == labios::CompletionState::Timeout);
    REQUIRE(first.results.size() == 1);
    CHECK(first.results.front().state == labios::CompletionState::Pending);

    context->set(7, labios::CompletionState::Complete, 200);
    CHECK(handle.wait_for(std::chrono::milliseconds(0)).state ==
          labios::CompletionState::Complete);
}

TEST_CASE("Concurrent wait and cancel share one safe Operation context",
          "[api-lifetime]") {
    auto context = std::make_shared<FakeContext>();
    context->set(9, labios::CompletionState::Pending);
    auto handle = operation(context, {9});
    std::atomic<int> observations{0};
    std::atomic<bool> malformed{false};

    std::jthread waiter([&] {
        for (int i = 0; i < 100; ++i) {
            (void)handle.wait_for(std::chrono::milliseconds(0));
            observations.fetch_add(1);
        }
    });
    std::jthread canceller([&] {
        for (int i = 0; i < 100; ++i) {
            const auto result = handle.cancel();
            if (result.size() != 1) malformed.store(true);
            observations.fetch_add(1);
        }
    });
    waiter.join();
    canceller.join();

    CHECK(observations.load() == 200);
    CHECK_FALSE(malformed.load());
    CHECK(handle.test().state == labios::CompletionState::Cancelled);
}

TEST_CASE("Read result retrieval is repeatable", "[api-lifetime]") {
    auto context = std::make_shared<FakeContext>();
    context->set(12, labios::CompletionState::Complete, 300);
    context->set_bytes({std::byte{1}, std::byte{2}});
    auto handle = operation(context, {12}, labios::OperationKind::Read);

    CHECK(handle.read() == handle.read());
    CHECK(handle.read().size() == 2);
}

TEST_CASE("Empty and wrong-kind Operations fail predictably", "[api-lifetime]") {
    labios::Operation empty;
    CHECK_THROWS_AS(empty.test(), labios::ClientError);

    auto context = std::make_shared<FakeContext>();
    context->set(13, labios::CompletionState::Complete);
    auto write = operation(context, {13});
    CHECK_THROWS_AS(write.read(), labios::ClientError);
}
