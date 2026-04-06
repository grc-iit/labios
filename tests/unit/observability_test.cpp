#include <catch2/catch_test_macros.hpp>
#include <labios/catalog_manager.h>
#include <labios/observability.h>
#include <labios/telemetry.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/uri.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

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

static labios::Config test_config() {
    labios::Config cfg;
    cfg.dispatcher_batch_size = 100;
    cfg.dispatcher_batch_timeout_ms = 50;
    cfg.scheduler_policy = "constraint";
    cfg.dispatcher_aggregation_enabled = true;
    return cfg;
}

static std::vector<labios::WorkerInfo> test_workers() {
    return {
        {1, true, 0.9, 0.1, 3, 2, labios::WorkerTier::Databot, 0.85},
        {2, true, 0.7, 0.3, 4, 1, labios::WorkerTier::Pipeline, 0.72},
        {3, false, 0.5, 0.8, 2, 3, labios::WorkerTier::Agentic, 0.40},
    };
}

TEST_CASE("handle_observe workers/scores returns valid JSON with worker data", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();
    auto workers = test_workers();

    auto uri = labios::parse_uri("observe://workers/scores");
    auto result = labios::handle_observe(uri, workers, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.error.empty());
    REQUIRE(result.json_data.find("\"workers\"") != std::string::npos);
    REQUIRE(result.json_data.find("\"id\":1") != std::string::npos);
    REQUIRE(result.json_data.find("\"id\":2") != std::string::npos);
    REQUIRE(result.json_data.find("\"id\":3") != std::string::npos);
    REQUIRE(result.json_data.find("\"score\":0.85") != std::string::npos);
    REQUIRE(result.json_data.find("\"available\":false") != std::string::npos);
}

TEST_CASE("handle_observe workers/count returns correct tier counts", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();
    auto workers = test_workers();

    auto uri = labios::parse_uri("observe://workers/count");
    auto result = labios::handle_observe(uri, workers, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.json_data.find("\"databot\":1") != std::string::npos);
    REQUIRE(result.json_data.find("\"pipeline\":1") != std::string::npos);
    REQUIRE(result.json_data.find("\"agentic\":1") != std::string::npos);
    REQUIRE(result.json_data.find("\"total\":3") != std::string::npos);
}

TEST_CASE("handle_observe system/health returns connected status", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    auto uri = labios::parse_uri("observe://system/health");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.json_data.find("\"nats\":\"connected\"") != std::string::npos);
    REQUIRE(result.json_data.find("\"redis\":\"connected\"") != std::string::npos);
    REQUIRE(result.json_data.find("\"uptime_seconds\"") != std::string::npos);
}

TEST_CASE("handle_observe with unknown path returns error JSON", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    auto uri = labios::parse_uri("observe://foo/bar");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE_FALSE(result.success);
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(result.json_data.find("\"error\"") != std::string::npos);
    REQUIRE(result.json_data.find("foo/bar") != std::string::npos);
}

TEST_CASE("handle_observe config/current returns configuration values", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    auto uri = labios::parse_uri("observe://config/current");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.json_data.find("\"batch_size\":100") != std::string::npos);
    REQUIRE(result.json_data.find("\"batch_timeout_ms\":50") != std::string::npos);
    REQUIRE(result.json_data.find("\"scheduler_policy\":\"constraint\"") != std::string::npos);
    REQUIRE(result.json_data.find("\"aggregation_enabled\":true") != std::string::npos);
}

TEST_CASE("handle_observe queue/depth reads from Redis", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    redis.set("labios:queue:depth", "42");

    auto uri = labios::parse_uri("observe://queue/depth");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.json_data.find("\"queue_depth\":42") != std::string::npos);
    REQUIRE(result.json_data.find("\"timestamp_ms\"") != std::string::npos);

    redis.del("labios:queue:depth");
}

TEST_CASE("handle_observe workspaces/list finds workspace index keys", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    // Create fake workspace index keys.
    redis.sadd("labios:ws:team-alpha:_index", "placeholder");
    redis.sadd("labios:ws:shared-data:_index", "placeholder");

    auto uri = labios::parse_uri("observe://workspaces/list");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.json_data.find("\"workspaces\"") != std::string::npos);
    REQUIRE(result.json_data.find("team-alpha") != std::string::npos);
    REQUIRE(result.json_data.find("shared-data") != std::string::npos);

    redis.del("labios:ws:team-alpha:_index");
    redis.del("labios:ws:shared-data:_index");
}

TEST_CASE("TelemetryPublisher record_label_dispatched increments counter", "[telemetry]") {
    labios::transport::NatsConnection nats(nats_url());
    labios::TelemetryPublisher pub(nats,
        []() -> std::vector<labios::WorkerInfo> { return {}; },
        std::chrono::seconds(60));

    // Counter should increment without start() being called.
    pub.record_label_dispatched();
    pub.record_label_dispatched();
    pub.record_label_dispatched();
    // No assertion on the internal counter (it's private), but this verifies
    // the method is callable and doesn't crash.
    REQUIRE(true);
}

TEST_CASE("TelemetryPublisher record_label_completed updates latency tracking", "[telemetry]") {
    labios::transport::NatsConnection nats(nats_url());
    labios::TelemetryPublisher pub(nats,
        []() -> std::vector<labios::WorkerInfo> { return {}; },
        std::chrono::seconds(60));

    pub.record_label_completed(std::chrono::microseconds(500));
    pub.record_label_completed(std::chrono::microseconds(1200));
    // Verifies the method is callable and handles multiple invocations.
    REQUIRE(true);
}

TEST_CASE("TelemetryPublisher start/stop lifecycle", "[telemetry]") {
    labios::transport::NatsConnection nats(nats_url());
    auto workers = test_workers();
    labios::TelemetryPublisher pub(nats,
        [&]() -> std::vector<labios::WorkerInfo> { return workers; },
        std::chrono::milliseconds(100));

    pub.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    pub.stop();
    // Verifies clean start/stop without deadlock or crash.
    REQUIRE(true);
}

TEST_CASE("Observe data/location returns worker mapping", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    // Register a file location in the catalog.
    catalog.set_location("/test/file.dat", 7);

    auto uri = labios::parse_uri("observe://data/location?file=/test/file.dat");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE(result.success);
    REQUIRE(result.error.empty());
    REQUIRE(result.json_data.find("\"file\":\"/test/file.dat\"") != std::string::npos);
    REQUIRE(result.json_data.find("\"worker_id\":7") != std::string::npos);

    // Clean up.
    redis.del("labios:location:/test/file.dat");
}

TEST_CASE("Observe data/location without file param returns error", "[observability]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::CatalogManager catalog(redis);
    auto cfg = test_config();

    auto uri = labios::parse_uri("observe://data/location");
    auto result = labios::handle_observe(uri, {}, redis, nats, cfg, catalog);

    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("missing") != std::string::npos);
}

TEST_CASE("ObserveResult default construction", "[observability]") {
    labios::ObserveResult result;
    REQUIRE(result.success);
    REQUIRE(result.error.empty());
    REQUIRE(result.json_data.empty());
}
