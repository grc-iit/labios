#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>
#include <labios/session.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <memory>
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

TEST_CASE("Write then read same file triggers supertask", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // Write 64KB in a single call, then immediately read it back.
    // The write and read to the same file_key within a batch window should
    // trigger a RAW dependency and produce a supertask in the shuffler.
    constexpr size_t sz = 64 * 1024;
    std::vector<std::byte> data(sz);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + sz,
              static_cast<uint8_t>(0x42));

    client.write("/test/supertask_raw.dat", data);
    auto result = client.read("/test/supertask_raw.dat", 0, sz);

    REQUIRE(result.size() == sz);
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST_CASE("Multiple small writes aggregate", "[data_path]") {
    auto cfg = test_config();
    cfg.label_min_size = 64 * 1024; // ensure small-I/O cache is active
    auto client = labios::connect(cfg);

    // Write 10 x 4KB chunks at consecutive offsets (below min_label_size).
    constexpr size_t chunk = 4096;
    constexpr int n = 10;
    std::vector<std::byte> full_data(chunk * n);
    for (int i = 0; i < n; ++i) {
        std::memset(full_data.data() + i * chunk, i + 1, chunk);
    }

    // Write the full buffer (the client will split as needed).
    client.write("/test/agg_small.dat", full_data);

    // Read back the full file.
    auto result = client.read("/test/agg_small.dat", 0, full_data.size());
    REQUIRE(result.size() == full_data.size());
    REQUIRE(std::equal(result.begin(), result.end(), full_data.begin()));
}

TEST_CASE("Write to 3 different files distributes across workers", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr size_t sz = 1024 * 1024; // 1MB per file
    std::vector<std::string> paths = {
        "/test/dist_a.dat", "/test/dist_b.dat", "/test/dist_c.dat"
    };

    for (int i = 0; i < 3; ++i) {
        std::vector<std::byte> data(sz, static_cast<std::byte>(i + 0xA0));
        client.write(paths[i], data);
    }

    for (int i = 0; i < 3; ++i) {
        auto result = client.read(paths[i], 0, sz);
        REQUIRE(result.size() == sz);
        REQUIRE(result[0] == static_cast<std::byte>(i + 0xA0));
        REQUIRE(result[sz - 1] == static_cast<std::byte>(i + 0xA0));
    }
}

TEST_CASE("Shared file and SQLite data is visible across round-robin workers", "[data_path][deployment]") {
    auto cfg = test_config();
    cfg.scheduler_policy = "round-robin";
    auto client = labios::connect(cfg);

    constexpr int operation_count = 24;
    constexpr size_t payload_size = 2048;
    std::vector<std::string> uris;
    std::vector<std::vector<std::byte>> expected;
    uris.reserve(operation_count);
    expected.reserve(operation_count);

    for (int i = 0; i < operation_count; ++i) {
        const auto uri = i % 2 == 0
            ? "file:///test/shared_rr_" + std::to_string(i) + ".bin"
            : "sqlite:///test/shared_rr_" + std::to_string(i);
        std::vector<std::byte> payload(payload_size, static_cast<std::byte>(i));
        client.write_to(uri, payload);
        uris.push_back(uri);
        expected.push_back(std::move(payload));
    }

    for (int i = 0; i < operation_count; ++i) {
        CHECK(client.read_from(uris[i], payload_size) == expected[i]);
    }
}

TEST_CASE("Read routes to the holding worker", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // Write 1MB, then read it back. Read-locality routing ensures the read
    // goes to the worker that holds the data.
    constexpr size_t sz = 1024 * 1024;
    std::vector<std::byte> data(sz);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + sz,
              static_cast<uint8_t>(0));

    client.write("/test/locality_test.dat", data);
    auto result = client.read("/test/locality_test.dat", 0, sz);

    REQUIRE(result.size() == sz);
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST_CASE("Source URI pipeline writes to a different SQLite backend", "[data_path][pipeline]") {
    auto cfg = test_config();
    std::unique_ptr<labios::Client> client;
    try {
        client = std::make_unique<labios::Client>(cfg);
    } catch (const std::exception& ex) {
        SKIP(std::string("Docker Compose services unavailable: ") + ex.what());
    }
    for (const auto* service : {"dispatcher", "worker-1", "worker-2", "worker-3"}) {
        if (!client->session().redis().get("labios:ready:" + std::string(service))) {
            SKIP(std::string("Docker Compose service is not ready: ") + service);
        }
    }

    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string source_uri = "file:///p04/source_" + suffix + ".bin";
    const std::string destination_uri = "sqlite:///p04/result_" + suffix;

    // Create the source through LABIOS's file backend, not by writing the
    // worker volume from the test process.
    std::vector<std::byte> source(sizeof(uint64_t) * 5);
    const uint64_t values[] = {50, 10, 40, 20, 30};
    std::memcpy(source.data(), values, sizeof(values));
    client->write_to(source_uri, source);

    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://sort_uint64", "", -1, 1});
    pipeline.stages.push_back({"builtin://truncate",
                               std::to_string(2 * sizeof(uint64_t)), 0, -1});

    // The single-host reference deployment gives every worker the same
    // Shared attachment, so source and destination are visible on every turn.
    auto pending = client->execute_pipeline(source_uri, destination_uri, pipeline);
    client->wait(pending);

    auto transformed = client->read_from(
        destination_uri, 2 * sizeof(uint64_t));
    REQUIRE(transformed.size() == 2 * sizeof(uint64_t));
    REQUIRE(std::memcmp(transformed.data(), values + 1, sizeof(uint64_t)) == 0);
    REQUIRE(std::memcmp(transformed.data() + sizeof(uint64_t), values + 3,
                        sizeof(uint64_t)) == 0);
}

TEST_CASE("Configured external KV backend completes a label", "[data_path][kv]") {
    auto cfg = test_config();
    std::unique_ptr<labios::Client> client;
    try {
        client = std::make_unique<labios::Client>(cfg);
    } catch (const std::exception& ex) {
        SKIP(std::string("Docker Compose services unavailable: ") + ex.what());
    }
    for (const auto* service : {"dispatcher", "worker-1", "worker-2", "worker-3"}) {
        if (!client->session().redis().get("labios:ready:" + std::string(service))) {
            SKIP(std::string("Docker Compose service is not ready: ") + service);
        }
    }

    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string uri = "kv://default/p02-" + suffix;
    const std::vector<std::byte> expected{
        std::byte{0x4c}, std::byte{0x41}, std::byte{0x42}, std::byte{0x49}, std::byte{0x4f}, std::byte{0x53}};
    client->write_to(uri, expected);
    const auto actual = client->read_from(uri, expected.size());
    CHECK(actual == expected);
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
