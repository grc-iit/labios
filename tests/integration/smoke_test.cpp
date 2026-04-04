#include <catch2/catch_test_macros.hpp>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

static std::string env_or(const char* name, const char* fallback) {
    const char* val = std::getenv(name);
    return (val != nullptr && val[0] != '\0') ? val : fallback;
}

static std::string nats_url() {
    return env_or("LABIOS_NATS_URL", "nats://localhost:4222");
}

static std::string redis_host() {
    return env_or("LABIOS_REDIS_HOST", "localhost");
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val != nullptr && val[0] != '\0') ? std::stoi(val) : 6379;
}

TEST_CASE("All services report ready", "[smoke]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());

    auto check = [&](const std::string& key) {
        auto val = redis.get(key);
        INFO("Checking readiness key: " << key);
        REQUIRE(val.has_value());
        REQUIRE(val.value() == "1");
    };

    check("labios:ready:dispatcher");
    check("labios:ready:worker-1");
    check("labios:ready:worker-2");
    check("labios:ready:worker-3");
    check("labios:ready:manager");
}

TEST_CASE("NATS message flows from client to worker", "[smoke]") {
    labios::transport::NatsConnection nats(nats_url());
    labios::transport::RedisConnection redis(redis_host(), redis_port());

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string msg_id = "smoke_" + std::to_string(now);

    // Publish to worker-1's subject
    nats.publish("labios.worker.1", msg_id);
    nats.drain();

    // Poll Redis for the confirmation key the worker writes
    std::string key = "labios:confirmation:" + msg_id;
    std::optional<std::string> result;
    for (int i = 0; i < 20; ++i) {
        result = redis.get(key);
        if (result.has_value()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "received_by_worker_1");
}
