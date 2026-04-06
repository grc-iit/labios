#include <catch2/catch_test_macros.hpp>
#include <labios/channel.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

static std::string nats_url() {
    const char* u = std::getenv("LABIOS_NATS_URL");
    return (u && u[0]) ? u : "nats://localhost:4222";
}

TEST_CASE("Channel publish and subscribe delivers data", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-pub-sub", redis, nats);

    std::atomic<int> received{0};
    labios::ChannelMessage last_msg{};
    std::mutex msg_mu;

    ch.subscribe([&](const labios::ChannelMessage& m) {
        std::lock_guard lock(msg_mu);
        last_msg.sequence = m.sequence;
        last_msg.label_id = m.label_id;
        last_msg.data = m.data;
        received.fetch_add(1);
    });

    std::vector<std::byte> data(64, static_cast<std::byte>(0xAA));
    uint64_t seq = ch.publish(data, 42);
    REQUIRE(seq > 0);

    // Wait for async delivery
    for (int i = 0; i < 50 && received.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(received.load() == 1);
    {
        std::lock_guard lock(msg_mu);
        REQUIRE(last_msg.sequence == seq);
        REQUIRE(last_msg.label_id == 42);
        REQUIRE(last_msg.data.size() == 64);
        REQUIRE(last_msg.data[0] == static_cast<std::byte>(0xAA));
    }

    ch.destroy();
}

TEST_CASE("Channel publish ordering is monotonic", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-ordering", redis, nats);

    std::vector<uint64_t> sequences;
    std::mutex seq_mu;

    ch.subscribe([&](const labios::ChannelMessage& m) {
        std::lock_guard lock(seq_mu);
        sequences.push_back(m.sequence);
    });

    std::vector<std::byte> data(16, static_cast<std::byte>(0xBB));
    for (int i = 0; i < 5; ++i) {
        ch.publish(data);
    }

    // Wait for all deliveries
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard lock(seq_mu);
        if (sequences.size() >= 5) break;
    }

    std::lock_guard lock(seq_mu);
    REQUIRE(sequences.size() == 5);
    for (size_t i = 1; i < sequences.size(); ++i) {
        REQUIRE(sequences[i] > sequences[i - 1]);
    }

    ch.destroy();
}

TEST_CASE("Two subscribers receive the same message", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-multi-sub", redis, nats);

    std::atomic<int> count_a{0};
    std::atomic<int> count_b{0};

    ch.subscribe([&](const labios::ChannelMessage&) { count_a.fetch_add(1); });
    ch.subscribe([&](const labios::ChannelMessage&) { count_b.fetch_add(1); });

    REQUIRE(ch.subscriber_count() == 2);

    std::vector<std::byte> data(8, static_cast<std::byte>(0xCC));
    ch.publish(data);

    for (int i = 0; i < 50 && (count_a.load() == 0 || count_b.load() == 0); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(count_a.load() == 1);
    REQUIRE(count_b.load() == 1);

    ch.destroy();
}

TEST_CASE("Unsubscribe removes callback", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-unsub", redis, nats);

    std::atomic<int> received{0};
    int sub_id = ch.subscribe([&](const labios::ChannelMessage&) {
        received.fetch_add(1);
    });

    REQUIRE(ch.subscriber_count() == 1);
    ch.unsubscribe(sub_id);
    REQUIRE(ch.subscriber_count() == 0);

    ch.destroy();
}

TEST_CASE("Channel auto-destroys after last unsubscribe during drain", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-drain", redis, nats);

    int sub_id = ch.subscribe([](const labios::ChannelMessage&) {});
    ch.drain();

    REQUIRE_FALSE(ch.is_destroyed());  // Still has a subscriber

    ch.unsubscribe(sub_id);
    REQUIRE(ch.is_destroyed());
}

TEST_CASE("Channel with TTL sets expiry on warehouse keys", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-ttl", redis, nats, 300);

    std::vector<std::byte> data(32, static_cast<std::byte>(0xDD));
    uint64_t seq = ch.publish(data);
    REQUIRE(seq > 0);

    // Verify data was staged (we can retrieve it)
    auto key = "labios:channel:test-ttl:" + std::to_string(seq);
    auto retrieved = redis.get_binary(key);
    REQUIRE(retrieved.size() == 32);
    REQUIRE(retrieved[0] == static_cast<std::byte>(0xDD));

    ch.destroy();
}

TEST_CASE("ChannelRegistry create, get, list, remove", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry registry(redis, nats);

    auto* ch1 = registry.create("reg-alpha");
    REQUIRE(ch1 != nullptr);
    REQUIRE(ch1->name() == "reg-alpha");

    auto* ch2 = registry.create("reg-beta", 60);
    REQUIRE(ch2 != nullptr);

    // Duplicate name returns nullptr
    auto* dup = registry.create("reg-alpha");
    REQUIRE(dup == nullptr);

    // Get by name
    REQUIRE(registry.get("reg-alpha") == ch1);
    REQUIRE(registry.get("reg-beta") == ch2);
    REQUIRE(registry.get("nonexistent") == nullptr);

    // List
    auto names = registry.list();
    REQUIRE(names.size() == 2);

    // Remove
    ch1->destroy();
    registry.remove("reg-alpha");
    REQUIRE(registry.get("reg-alpha") == nullptr);
    REQUIRE(registry.list().size() == 1);

    ch2->destroy();
    registry.remove("reg-beta");
}

TEST_CASE("Publish to destroyed channel returns zero", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-destroyed-pub", redis, nats);

    ch.destroy();
    REQUIRE(ch.is_destroyed());

    std::vector<std::byte> data(16, static_cast<std::byte>(0xEE));
    uint64_t seq = ch.publish(data);
    REQUIRE(seq == 0);
}

TEST_CASE("Publish to draining channel returns zero", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-draining-pub", redis, nats);

    ch.drain();

    std::vector<std::byte> data(16, static_cast<std::byte>(0xFF));
    uint64_t seq = ch.publish(data);
    REQUIRE(seq == 0);
}

TEST_CASE("Channel with zero subscribers stages data in warehouse", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-no-sub-stage", redis, nats);

    std::vector<std::byte> data(48, static_cast<std::byte>(0x77));
    uint64_t seq = ch.publish(data);
    REQUIRE(seq > 0);

    // Data should be in warehouse even with no subscribers
    auto key = "labios:channel:test-no-sub-stage:" + std::to_string(seq);
    auto retrieved = redis.get_binary(key);
    REQUIRE(retrieved.size() == 48);
    REQUIRE(retrieved[0] == static_cast<std::byte>(0x77));

    ch.destroy();
}

TEST_CASE("Destroy cleans up warehouse keys", "[channel]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::Channel ch("test-cleanup", redis, nats);

    std::vector<std::byte> data(16, static_cast<std::byte>(0x11));
    uint64_t seq1 = ch.publish(data);
    uint64_t seq2 = ch.publish(data);

    auto key1 = "labios:channel:test-cleanup:" + std::to_string(seq1);
    auto key2 = "labios:channel:test-cleanup:" + std::to_string(seq2);

    // Keys exist before destroy
    REQUIRE(redis.get_binary(key1).size() == 16);
    REQUIRE(redis.get_binary(key2).size() == 16);

    ch.destroy();

    // Keys cleaned up after destroy
    REQUIRE(redis.get_binary(key1).empty());
    REQUIRE(redis.get_binary(key2).empty());
}
