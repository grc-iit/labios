#include <catch2/catch_test_macros.hpp>
#include <labios/label_manager.h>
#include <labios/content_manager.h>
#include <labios/catalog_manager.h>
#include <labios/transport/nats.h>
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

static std::string nats_url() {
    const char* u = std::getenv("LABIOS_NATS_URL");
    return (u && u[0]) ? u : "nats://localhost:4222";
}

TEST_CASE("label_count computes correct split count", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    REQUIRE(lm.label_count(0) == 0);
    REQUIRE(lm.label_count(1) == 1);
    REQUIRE(lm.label_count(1048576) == 1);
    REQUIRE(lm.label_count(1048577) == 2);
    REQUIRE(lm.label_count(10485760) == 10);
    REQUIRE(lm.label_count(500000) == 1);
}

TEST_CASE("publish_write splits 2MB into 2 labels", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    std::vector<std::byte> data(2 * 1048576);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    auto pending = lm.publish_write("/test/split_2mb.bin", 0, data);
    REQUIRE(pending.size() == 2);
    REQUIRE(pending[0].label_id != 0);
    REQUIRE(pending[1].label_id != 0);
    REQUIRE(pending[0].label_id != pending[1].label_id);

    lm.wait(pending);
}

TEST_CASE("Split write then split read returns original data", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    std::vector<std::byte> data(3 * 1048576);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    auto write_pending = lm.publish_write("/test/split_3mb.bin", 0, data);
    REQUIRE(write_pending.size() == 3);
    lm.wait(write_pending);

    auto read_pending = lm.publish_read("/test/split_3mb.bin", 0, data.size());
    REQUIRE(read_pending.size() == 3);
    auto result = lm.wait_read(read_pending);

    REQUIRE(result.size() == data.size());
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST_CASE("publish_write with zero-length data returns no labels", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    std::vector<std::byte> empty;
    auto pending = lm.publish_write("/test/empty.bin", 0, empty);
    REQUIRE(pending.empty());
    lm.wait(pending);
}

TEST_CASE("publish_read with zero size returns no labels and empty result", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    auto pending = lm.publish_read("/test/empty.bin", 0, 0);
    REQUIRE(pending.empty());
    auto result = lm.wait_read(pending);
    REQUIRE(result.empty());
}

TEST_CASE("wait propagates async reply timeout", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1, /*reply_timeout_ms=*/1);

    labios::PendingLabel pending;
    pending.label_id = 123;
    pending.async_reply = std::make_shared<labios::transport::AsyncReply>();

    std::array<labios::PendingLabel, 1> entries{pending};
    REQUIRE_THROWS(lm.wait(entries));
    REQUIRE_THROWS(lm.wait_read(entries));
}
