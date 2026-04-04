#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

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

TEST_CASE("Write 1MB and read it back", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    std::vector<std::byte> data(1024 * 1024);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    client.write("/test/data_path_1mb.bin", data);
    auto result = client.read("/test/data_path_1mb.bin", 0, data.size());

    REQUIRE(result.size() == data.size());
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST_CASE("Cross-session: write in one client, read from another", "[data_path]") {
    auto cfg = test_config();

    // Write with client A
    {
        auto clientA = labios::connect(cfg);
        std::vector<std::byte> data(2048);
        std::iota(reinterpret_cast<uint8_t*>(data.data()),
                  reinterpret_cast<uint8_t*>(data.data()) + data.size(),
                  static_cast<uint8_t>(0xAA));
        clientA.write("/test/cross_api.bin", data);
    }

    // Read with a separate client B (simulates different process)
    {
        auto clientB = labios::connect(cfg);
        auto result = clientB.read("/test/cross_api.bin", 0, 2048);
        REQUIRE(result.size() == 2048);
        REQUIRE(result[0] == static_cast<std::byte>(0xAA));
    }
}

TEST_CASE("Write 10 labels and verify all complete", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    for (int i = 0; i < 10; ++i) {
        std::vector<std::byte> data(1024, static_cast<std::byte>(i));
        std::string path = "/test/batch_" + std::to_string(i) + ".bin";
        client.write(path, data);
    }

    // Verify all files can be read back
    for (int i = 0; i < 10; ++i) {
        std::string path = "/test/batch_" + std::to_string(i) + ".bin";
        auto result = client.read(path, 0, 1024);
        REQUIRE(result.size() == 1024);
        REQUIRE(result[0] == static_cast<std::byte>(i));
    }
}
