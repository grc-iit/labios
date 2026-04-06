// tests/integration/elastic_flood_test.cpp
#include <labios/config.h>
#include <labios/label.h>
#include <labios/content_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

int main(int argc, char* argv[]) {
    int label_count = 10000;
    if (argc > 1) label_count = std::stoi(argv[1]);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);

    // Wait for dispatcher readiness.
    for (int i = 0; i < 30; ++i) {
        auto val = redis.get("labios:ready:dispatcher");
        if (val && *val == "1") break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "[" << timestamp() << "] flooding " << label_count
              << " labels...\n" << std::flush;

    auto start = std::chrono::steady_clock::now();

    std::vector<std::byte> payload(1024, std::byte{0x42});

    for (int i = 0; i < label_count; ++i) {
        labios::LabelData label{};
        label.id = labios::generate_label_id(0);
        label.type = labios::LabelType::Write;
        label.destination = labios::FilePath{
            "/labios/flood/file_" + std::to_string(i) + ".dat", 0,
            static_cast<uint64_t>(payload.size())};
        label.data_size = payload.size();

        redis.set_binary(labios::ContentManager::data_key(label.id),
                         std::span<const std::byte>(payload));

        auto serialized = labios::serialize_label(label);
        nats.publish("labios.labels",
                     std::span<const std::byte>(serialized));

        if ((i + 1) % 100 == 0) {
            nats.flush();
        }
    }
    nats.flush();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    std::cout << "[" << timestamp() << "] published " << label_count
              << " labels in " << ms << "ms ("
              << (label_count * 1000 / std::max(ms, 1L)) << " labels/s)\n"
              << std::flush;

    std::cout << "[" << timestamp() << "] flood complete. Watch manager logs for scaling.\n";
    return 0;
}
