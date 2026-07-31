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
    REQUIRE(pending[0] != 0);
    REQUIRE(pending[1] != 0);
    REQUIRE(pending[0] != pending[1]);

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

TEST_CASE("wait returns typed timeout without cancelling the label", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1, /*reply_timeout_ms=*/1);

    catalog.create(123, 1, labios::LabelType::Read);
    std::array<uint64_t, 1> entries{123};
    const auto waited = lm.wait(entries, std::chrono::milliseconds(1));
    REQUIRE(waited.state == labios::CompletionState::Timeout);
    REQUIRE(waited.results.size() == 1);
    CHECK(waited.results.front().state == labios::CompletionState::Pending);
    CHECK_FALSE(waited.results.front().terminal());

    try {
        (void)lm.wait_read(entries, std::chrono::milliseconds(1));
        FAIL("expected typed read timeout");
    } catch (const labios::CompletionError& ex) {
        CHECK(ex.state() == labios::CompletionState::Timeout);
        CHECK(ex.label_id() == 123);
        CHECK(std::string(ex.what()).find("remains active") != std::string::npos);
    }
}

TEST_CASE("test reports the catalog-authoritative lifecycle", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    labios::LabelData label;
    label.id = labios::generate_label_id(1);
    label.app_id = 1;
    label.type = labios::LabelType::Read;
    labios::mark_label_created(label);
    catalog.create(label);
    CHECK(lm.test(label.id).lifecycle == labios::LifecycleState::Submitted);

    catalog.set_status(label.id, labios::LabelStatus::Queued);
    CHECK(lm.test(label.id).lifecycle == labios::LifecycleState::Queued);
    labios::mark_label_queued(label);
    labios::mark_label_shuffled(label);
    catalog.persist_snapshot(label);
    CHECK(lm.test(label.id).lifecycle == labios::LifecycleState::Shuffled);
    catalog.set_status(label.id, labios::LabelStatus::Scheduled);
    CHECK(lm.test(label.id).lifecycle == labios::LifecycleState::Scheduled);
    catalog.set_status(label.id, labios::LabelStatus::Executing);
    CHECK(lm.test(label.id).lifecycle == labios::LifecycleState::Executing);

    labios::CompletionData completion;
    completion.label_id = label.id;
    completion.status = labios::CompletionStatus::Complete;
    catalog.set_completion(completion);
    const auto completed = lm.test(label.id);
    CHECK(completed.state == labios::CompletionState::Complete);
    CHECK(completed.lifecycle == labios::LifecycleState::Completed);

    const auto unknown = lm.test(label.id + 1);
    CHECK(unknown.state == labios::CompletionState::Unknown);
    CHECK(unknown.lifecycle == labios::LifecycleState::Unknown);
}

TEST_CASE("test exposes parked reason and retry metadata", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    labios::LabelData label;
    label.id = labios::generate_label_id(1);
    label.app_id = 1;
    label.type = labios::LabelType::Read;
    catalog.create(label);
    catalog.park(label, "NO_WORKERS", 3, 424242);

    const auto result = lm.test(label.id);
    CHECK(result.state == labios::CompletionState::Parked);
    CHECK(result.park_reason == "NO_WORKERS");
    CHECK(result.park_attempts == 3);
    CHECK(result.next_retry_at_ms == 424242);
}
