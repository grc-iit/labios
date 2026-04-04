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
