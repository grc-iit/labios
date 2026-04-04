#include <catch2/catch_test_macros.hpp>
#include <labios/config.h>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

static fs::path write_temp_toml(const std::string& content) {
    auto path = fs::temp_directory_path() / "labios_test_config.toml";
    std::ofstream out(path);
    out << content;
    return path;
}

TEST_CASE("load_config reads TOML values", "[config]") {
    auto path = write_temp_toml(R"(
[nats]
url = "nats://testhost:4222"

[redis]
host = "redis-test"
port = 7777

[worker]
id = 42
speed = 5
capacity = "100GB"
)");

    auto cfg = labios::load_config(path);
    REQUIRE(cfg.nats_url == "nats://testhost:4222");
    REQUIRE(cfg.redis_host == "redis-test");
    REQUIRE(cfg.redis_port == 7777);
    REQUIRE(cfg.worker_id == 42);
    REQUIRE(cfg.worker_speed == 5);
    REQUIRE(cfg.worker_capacity == "100GB");

    fs::remove(path);
}

TEST_CASE("load_config uses defaults for missing file", "[config]") {
    auto cfg = labios::load_config("/nonexistent/path.toml");
    REQUIRE(cfg.nats_url == "nats://localhost:4222");
    REQUIRE(cfg.redis_host == "localhost");
    REQUIRE(cfg.redis_port == 6379);
}

TEST_CASE("environment variables override TOML values", "[config]") {
    auto path = write_temp_toml(R"(
[nats]
url = "nats://toml-host:4222"

[redis]
host = "toml-redis"
port = 6379
)");

    setenv("LABIOS_NATS_URL", "nats://env-host:9999", 1);
    setenv("LABIOS_REDIS_HOST", "env-redis", 1);

    auto cfg = labios::load_config(path);
    REQUIRE(cfg.nats_url == "nats://env-host:9999");
    REQUIRE(cfg.redis_host == "env-redis");

    unsetenv("LABIOS_NATS_URL");
    unsetenv("LABIOS_REDIS_HOST");
    fs::remove(path);
}

TEST_CASE("parse_size handles units", "[config]") {
    REQUIRE(labios::parse_size("64KB") == 65536);
    REQUIRE(labios::parse_size("1MB") == 1048576);
    REQUIRE(labios::parse_size("2GB") == 2147483648ULL);
    REQUIRE(labios::parse_size("4096") == 4096);
    REQUIRE(labios::parse_size("") == 0);
}

TEST_CASE("load_config reads label and cache settings", "[config]") {
    auto path = write_temp_toml(R"(
[nats]
url = "nats://localhost:4222"

[label]
min_size = "128KB"
max_size = "4MB"

[cache]
flush_interval_ms = 1000
default_read_policy = "write-only"

[intercept]
prefixes = ["/labios", "/scratch"]
)");

    auto cfg = labios::load_config(path);
    REQUIRE(cfg.label_min_size == 128 * 1024);
    REQUIRE(cfg.label_max_size == 4 * 1024 * 1024);
    REQUIRE(cfg.cache_flush_interval_ms == 1000);
    REQUIRE(cfg.cache_read_policy == "write-only");
    REQUIRE(cfg.intercept_prefixes.size() == 2);
    REQUIRE(cfg.intercept_prefixes[0] == "/labios");
    REQUIRE(cfg.intercept_prefixes[1] == "/scratch");

    fs::remove(path);
}

TEST_CASE("Dispatcher config parsed from TOML", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "labios_cfg_disp.toml";
    {
        std::ofstream f(tmp);
        f << "[dispatcher]\n"
          << "batch_size = 200\n"
          << "batch_timeout_ms = 75\n"
          << "aggregation_enabled = false\n"
          << "dep_granularity = \"per-application\"\n";
    }
    auto cfg = labios::load_config(tmp);
    CHECK(cfg.dispatcher_batch_size == 200);
    CHECK(cfg.dispatcher_batch_timeout_ms == 75);
    CHECK(cfg.dispatcher_aggregation_enabled == false);
    CHECK(cfg.dispatcher_dep_granularity == "per-application");
    std::filesystem::remove(tmp);
}

TEST_CASE("Dispatcher config defaults", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "labios_cfg_empty.toml";
    { std::ofstream f(tmp); f << "\n"; }
    auto cfg = labios::load_config(tmp);
    CHECK(cfg.dispatcher_batch_size == 100);
    CHECK(cfg.dispatcher_batch_timeout_ms == 50);
    CHECK(cfg.dispatcher_aggregation_enabled == true);
    CHECK(cfg.dispatcher_dep_granularity == "per-file");
    std::filesystem::remove(tmp);
}
