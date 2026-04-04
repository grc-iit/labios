#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>
#include <labios/transport/redis.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
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

TEST_CASE("Write and read back through full pipeline", "[smoke]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // 64KB of known data
    std::vector<std::byte> data(64 * 1024);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    client.write("/smoke/roundtrip.bin", data);
    auto result = client.read("/smoke/roundtrip.bin", 0, data.size());

    REQUIRE(result.size() == data.size());
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}
