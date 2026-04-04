#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

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

TEST_CASE("M3 demo: write 100 labels, read back, verify", "[scheduling]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr size_t label_size = 64 * 1024;  // 64KB
    constexpr int count = 100;

    std::vector<std::byte> data(label_size);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + label_size,
              static_cast<uint8_t>(0));

    for (int i = 0; i < count; ++i) {
        std::string path = "/labios/sched_test_" + std::to_string(i) + ".bin";
        client.write(path, data);
    }

    for (int i = 0; i < count; ++i) {
        std::string path = "/labios/sched_test_" + std::to_string(i) + ".bin";
        auto result = client.read(path, 0, label_size);
        REQUIRE(result.size() == label_size);
        CHECK(std::equal(result.begin(), result.end(), data.begin()));
    }
}
