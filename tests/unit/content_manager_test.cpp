#include <catch2/catch_test_macros.hpp>
#include <labios/content_manager.h>
#include <labios/transport/redis.h>

#include <cstdlib>
#include <numeric>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

TEST_CASE("Cache accumulates small writes and flushes at threshold", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(1024, static_cast<std::byte>(0xAB));

    auto r1 = cm.cache_write(10, "/test/cache.bin", 0, data);
    REQUIRE(r1.empty());
    auto r2 = cm.cache_write(10, "/test/cache.bin", 1024, data);
    REQUIRE(r2.empty());
    auto r3 = cm.cache_write(10, "/test/cache.bin", 2048, data);
    REQUIRE(r3.empty());

    // 4th write pushes to 4KB = threshold
    auto r4 = cm.cache_write(10, "/test/cache.bin", 3072, data);
    REQUIRE(r4.size() == 4);
    REQUIRE(r4[0].offset == 0);
    REQUIRE(r4[0].data.size() == 1024);
    REQUIRE(r4[3].offset == 3072);
}

TEST_CASE("Explicit flush returns all cached data", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(100, static_cast<std::byte>(0xCD));
    cm.cache_write(20, "/test/flush.bin", 0, data);
    cm.cache_write(20, "/test/flush.bin", 100, data);

    auto regions = cm.flush(20);
    REQUIRE(regions.size() == 2);
    REQUIRE(regions[0].filepath == "/test/flush.bin");
}

TEST_CASE("Read-through returns cached data", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(256);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + 256,
              static_cast<uint8_t>(0));

    cm.cache_write(30, "/test/rt.bin", 0, data);

    auto result = cm.cache_read(30, 0, 256);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 256);
    REQUIRE((*result)[0] == std::byte{0});
    REQUIRE((*result)[255] == std::byte{255});
}

TEST_CASE("Read-through with range overlap returns data from covering region",
          "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    // Write 1024 bytes at offset 0
    std::vector<std::byte> data(1024);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + 1024,
              static_cast<uint8_t>(0));

    cm.cache_write(35, "/test/overlap.bin", 0, data);

    // Read 256 bytes at offset 512 (within the cached region)
    auto result = cm.cache_read(35, 512, 256);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 256);
    REQUIRE((*result)[0] == data[512]);
    REQUIRE((*result)[255] == data[767]);
}

TEST_CASE("Read-through with adjacent regions assembles full range",
          "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    // Write two adjacent 512-byte regions
    std::vector<std::byte> chunk1(512, std::byte{0xAA});
    std::vector<std::byte> chunk2(512, std::byte{0xBB});

    cm.cache_write(36, "/test/adjacent.bin", 0, chunk1);
    cm.cache_write(36, "/test/adjacent.bin", 512, chunk2);

    // Read the full 1024 bytes spanning both regions
    auto result = cm.cache_read(36, 0, 1024);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1024);
    REQUIRE((*result)[0] == std::byte{0xAA});
    REQUIRE((*result)[511] == std::byte{0xAA});
    REQUIRE((*result)[512] == std::byte{0xBB});
    REQUIRE((*result)[1023] == std::byte{0xBB});
}

TEST_CASE("cache_read returns nullopt for partially covered range",
          "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(256, std::byte{0xCC});
    cm.cache_write(37, "/test/partial.bin", 0, data);

    // Request 512 bytes but only 256 are cached
    auto result = cm.cache_read(37, 0, 512);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Write-only cache_read returns nullopt", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::WriteOnly);

    std::vector<std::byte> data(256, std::byte{0});
    cm.cache_write(40, "/test/wo.bin", 0, data);

    auto result = cm.cache_read(40, 0, 256);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Evict removes cache state", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(100, std::byte{0});
    cm.cache_write(50, "/test/evict.bin", 0, data);
    cm.evict(50);

    auto regions = cm.flush(50);
    REQUIRE(regions.empty());
}

TEST_CASE("Warehouse stage and retrieve", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(512);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + 512,
              static_cast<uint8_t>(0));

    cm.stage(99999, data);
    REQUIRE(cm.exists(99999));

    auto result = cm.retrieve(99999);
    REQUIRE(result.size() == 512);
    REQUIRE(result == data);

    cm.remove(99999);
    REQUIRE_FALSE(cm.exists(99999));
}
