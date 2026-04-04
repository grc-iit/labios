#include <labios/config.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
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

    // Redis must outlive nats: the NATS callback captures &redis, so redis
    // must still be valid when nats drains its subscriptions during destruction.
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);

    std::string worker_subject = "labios.worker." + std::to_string(cfg.worker_id);
    std::string worker_name = "worker-" + std::to_string(cfg.worker_id);

    // Mutex protects redis from concurrent access: the NATS callback runs on
    // a cnats-managed thread while the main thread may also use redis.
    std::mutex redis_mu;

    int worker_id = cfg.worker_id;
    nats.subscribe(worker_subject,
        [&redis, &redis_mu, worker_id](std::string_view /*subject*/,
                                       std::span<const std::byte> data) {
            std::string msg_id(reinterpret_cast<const char*>(data.data()),
                               data.size());
            std::cout << "[" << timestamp() << "] worker " << worker_id
                      << ": received message " << msg_id << "\n" << std::flush;

            std::string key = "labios:confirmation:" + msg_id;
            std::string val = "received_by_worker_" + std::to_string(worker_id);
            std::lock_guard lock(redis_mu);
            redis.set(key, val);
        });

    {
        std::lock_guard lock(redis_mu);
        redis.set("labios:ready:" + worker_name, "1");
    }

    // Signal healthcheck
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] " << worker_name
              << " ready (speed=" << cfg.worker_speed
              << ", capacity=" << cfg.worker_capacity << ")\n"
              << std::flush;

    g_service_thread = std::jthread([](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    g_service_thread.join();

    std::cout << "[" << timestamp() << "] " << worker_name
              << " shutting down\n";
    return 0;
}
