#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
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

TEST_CASE("load_weight_profile reads TOML weights", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "test_profile.toml";
    {
        std::ofstream f(tmp);
        f << "[weights]\navailability = 0.5\ncapacity = 0.0\n"
          << "load = 0.35\nspeed = 0.15\nenergy = 0.0\n";
    }
    auto wp = labios::load_weight_profile(tmp);
    CHECK(wp.name == "test_profile");
    CHECK(wp.availability == Catch::Approx(0.5));
    CHECK(wp.capacity == Catch::Approx(0.0));
    CHECK(wp.load == Catch::Approx(0.35));
    CHECK(wp.speed == Catch::Approx(0.15));
    CHECK(wp.energy == Catch::Approx(0.0));
    std::filesystem::remove(tmp);
}

TEST_CASE("Config reads scheduler policy and profile path", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "sched_test.toml";
    {
        std::ofstream f(tmp);
        f << "[scheduler]\npolicy = \"constraint\"\nprofile_path = \"/etc/labios/profiles/low_latency.toml\"\n";
    }
    auto cfg = labios::load_config(tmp);
    CHECK(cfg.scheduler_policy == "constraint");
    CHECK(cfg.scheduler_profile_path == "/etc/labios/profiles/low_latency.toml");
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

TEST_CASE("Config reads elastic settings", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "elastic_cfg_test.toml";
    {
        std::ofstream f(tmp);
        f << "[elastic]\nenabled = true\nmin_workers = 2\nmax_workers = 5\n"
          << "pressure_threshold = 3\nworker_idle_timeout_ms = 10000\n"
          << "decommission_timeout_ms = 30000\ncommission_cooldown_ms = 2000\n"
          << "eval_interval_ms = 1000\n"
          << "docker_socket = \"/var/run/docker.sock\"\n"
          << "docker_image = \"labios-worker\"\n"
          << "docker_network = \"labios_default\"\n"
          << "elastic_worker_speed = 4\n"
          << "elastic_worker_energy = 2\n"
          << "elastic_worker_capacity = \"100GB\"\n";
    }
    auto cfg = labios::load_config(tmp);
    CHECK(cfg.elastic.enabled == true);
    CHECK(cfg.elastic.min_workers == 2);
    CHECK(cfg.elastic.max_workers == 5);
    CHECK(cfg.elastic.pressure_threshold == 3);
    CHECK(cfg.elastic.worker_idle_timeout_ms == 10000);
    CHECK(cfg.elastic.decommission_timeout_ms == 30000);
    CHECK(cfg.elastic.commission_cooldown_ms == 2000);
    CHECK(cfg.elastic.eval_interval_ms == 1000);
    CHECK(cfg.elastic.docker_socket == "/var/run/docker.sock");
    CHECK(cfg.elastic.docker_image == "labios-worker");
    CHECK(cfg.elastic.docker_network == "labios_default");
    CHECK(cfg.elastic.elastic_worker_speed == 4);
    CHECK(cfg.elastic.elastic_worker_energy == 2);
    CHECK(cfg.elastic.elastic_worker_capacity == "100GB");
    std::filesystem::remove(tmp);
}

TEST_CASE("load_weight_profile reads tier weight", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "agentic_profile.toml";
    {
        std::ofstream f(tmp);
        f << "[weights]\navailability = 0.3\ncapacity = 0.1\n"
          << "load = 0.1\nspeed = 0.1\nenergy = 0.0\ntier = 0.4\n";
    }
    auto wp = labios::load_weight_profile(tmp);
    CHECK(wp.name == "agentic_profile");
    CHECK(wp.tier == Catch::Approx(0.4));
    CHECK(wp.availability == Catch::Approx(0.3));
    std::filesystem::remove(tmp);
}

TEST_CASE("load_weight_profile defaults tier to zero", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "no_tier_profile.toml";
    {
        std::ofstream f(tmp);
        f << "[weights]\navailability = 0.5\nspeed = 0.5\n";
    }
    auto wp = labios::load_weight_profile(tmp);
    CHECK(wp.tier == Catch::Approx(0.0));
    std::filesystem::remove(tmp);
}

TEST_CASE("Config reads worker tier from TOML", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "tier_cfg_test.toml";
    {
        std::ofstream f(tmp);
        f << "[worker]\ntier = 2\n";
    }
    auto cfg = labios::load_config(tmp);
    CHECK(cfg.worker_tier == 2);
    std::filesystem::remove(tmp);
}

TEST_CASE("Config::set modifies runtime parameters", "[config]") {
    labios::Config cfg;
    CHECK(cfg.dispatcher_batch_size == 100);

    CHECK(cfg.set("batch_size", "250") == true);
    CHECK(cfg.dispatcher_batch_size == 250);

    CHECK(cfg.set("batch_timeout_ms", "200") == true);
    CHECK(cfg.dispatcher_batch_timeout_ms == 200);

    CHECK(cfg.set("scheduler_policy", "constraint") == true);
    CHECK(cfg.scheduler_policy == "constraint");

    CHECK(cfg.set("aggregation_enabled", "false") == true);
    CHECK(cfg.dispatcher_aggregation_enabled == false);

    CHECK(cfg.set("nonexistent_key", "val") == false);
}

TEST_CASE("Worker tier defaults to zero", "[config]") {
    auto cfg = labios::load_config("/nonexistent/path.toml");
    CHECK(cfg.worker_tier == 0);
}

TEST_CASE("Elastic config defaults when section missing", "[config]") {
    auto tmp = std::filesystem::temp_directory_path() / "no_elastic_test.toml";
    {
        std::ofstream f(tmp);
        f << "[nats]\nurl = \"nats://localhost:4222\"\n";
    }
    auto cfg = labios::load_config(tmp);
    CHECK(cfg.elastic.enabled == false);
    CHECK(cfg.elastic.min_workers == 1);
    CHECK(cfg.elastic.max_workers == 10);
    CHECK(cfg.elastic.pressure_threshold == 5);
    CHECK(cfg.elastic.worker_idle_timeout_ms == 30000);
    CHECK(cfg.elastic.decommission_timeout_ms == 60000);
    CHECK(cfg.elastic.commission_cooldown_ms == 5000);
    std::filesystem::remove(tmp);
}
