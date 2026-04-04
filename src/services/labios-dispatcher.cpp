#include <labios/config.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

static std::jthread g_service_thread;

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

static void signal_handler(int /*sig*/) {
    if (g_service_thread.joinable()) {
        g_service_thread.request_stop();
    }
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);

    nats.subscribe("labios.labels",
        [](std::string_view /*subject*/, std::span<const std::byte> data) {
            std::cout << "[" << timestamp() << "] dispatcher: received label ("
                      << data.size() << " bytes)\n";
        });

    redis.set("labios:ready:dispatcher", "1");

    // Signal healthcheck
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] dispatcher ready\n" << std::flush;

    g_service_thread = std::jthread([](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    g_service_thread.join();

    std::cout << "[" << timestamp() << "] dispatcher shutting down\n";
    return 0;
}
