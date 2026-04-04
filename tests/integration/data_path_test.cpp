#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/catalog_manager.h>
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

TEST_CASE("Write 10 labels and verify all complete", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::CatalogManager catalog(redis);

    std::vector<uint64_t> label_ids;
    for (int i = 0; i < 10; ++i) {
        std::vector<std::byte> data(1024, static_cast<std::byte>(i));
        std::string path = "/test/batch_" + std::to_string(i) + ".bin";

        labios::LabelParams params;
        params.type = labios::LabelType::Write;
        params.source = labios::memory_ptr(data.data(), data.size());
        params.destination = labios::file_path(path);
        auto label = client.create_label(params);

        auto status = client.publish(label, data);
        REQUIRE(status.result() == labios::CompletionStatus::Complete);
        label_ids.push_back(status.label_id());
    }

    for (auto id : label_ids) {
        auto status = catalog.get_status(id);
        REQUIRE(status == labios::LabelStatus::Complete);
    }
}
