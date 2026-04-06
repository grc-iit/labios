#include <catch2/catch_test_macros.hpp>
#include <labios/transport/nats.h>

#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("AsyncReply wait times out when no reply arrives", "[transport][nats]") {
    labios::transport::AsyncReply reply;
    REQUIRE_THROWS(reply.wait(std::chrono::milliseconds(1)));
}

TEST_CASE("AsyncReply wait returns buffered data after completion", "[transport][nats]") {
    labios::transport::AsyncReply reply;
    std::vector<std::byte> payload(8, std::byte{0xAB});

    std::thread producer([&reply, payload]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {
            std::lock_guard lock(reply.mu);
            reply.data = std::move(payload);
            reply.completed = true;
        }
        reply.cv.notify_one();
    });

    auto received = reply.wait(std::chrono::milliseconds(100));
    producer.join();

    REQUIRE(received == std::vector<std::byte>(8, std::byte{0xAB}));
}
