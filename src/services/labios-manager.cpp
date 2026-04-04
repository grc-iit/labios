#include <labios/config.h>
#include <labios/worker_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/elastic/docker_client.h>
#include <labios/elastic/orchestrator.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

static std::jthread g_service_thread;
static std::jthread g_elastic_thread;

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

    labios::InMemoryWorkerManager worker_mgr;

    // Elastic orchestrator (conditionally enabled).
    std::unique_ptr<labios::elastic::DockerClient> docker_client;
    using OrchestratorType = labios::elastic::Orchestrator<labios::elastic::DockerClient>;
    std::unique_ptr<OrchestratorType> orchestrator;

    if (cfg.elastic.enabled) {
        docker_client = std::make_unique<labios::elastic::DockerClient>(
            cfg.elastic.docker_socket);
        orchestrator = std::make_unique<OrchestratorType>(
            worker_mgr, *docker_client, cfg);

        std::cout << "[" << timestamp()
                  << "] manager: elastic mode enabled (min=" << cfg.elastic.min_workers
                  << ", max=" << cfg.elastic.max_workers
                  << ", image=" << cfg.elastic.docker_image << ")\n" << std::flush;
    }

    // Combined handler for all manager subjects.
    auto handler = [&](std::string_view subject,
                       std::span<const std::byte> data,
                       std::string_view reply_to) {
        std::string msg(reinterpret_cast<const char*>(data.data()), data.size());

        if (subject == "labios.worker.register") {
            // Parse: "id,speed,energy,capacity"
            std::istringstream iss(msg);
            std::string token;
            int id = 0, speed = 1, energy = 1;
            std::string capacity_str;

            if (std::getline(iss, token, ',')) id = std::stoi(token);
            if (std::getline(iss, token, ',')) speed = std::stoi(token);
            if (std::getline(iss, token, ',')) energy = std::stoi(token);
            if (std::getline(iss, token, ',')) capacity_str = token;

            double cap_ratio = 1.0;
            if (!capacity_str.empty() && cfg.max_worker_capacity > 0) {
                uint64_t cap_bytes = labios::parse_size(capacity_str);
                cap_ratio = std::min(
                    static_cast<double>(cap_bytes) /
                    static_cast<double>(cfg.max_worker_capacity),
                    1.0);
            }

            labios::WorkerInfo info{id, true, cap_ratio, 0.0, speed, energy};
            worker_mgr.register_worker(info);

            std::cout << "[" << timestamp() << "] manager: registered worker "
                      << id << " (speed=" << speed << ", energy=" << energy
                      << ", capacity=" << capacity_str << ")\n" << std::flush;

        } else if (subject == "labios.worker.deregister") {
            int id = std::stoi(msg);
            worker_mgr.deregister_worker(id);

            std::cout << "[" << timestamp() << "] manager: deregistered worker "
                      << id << "\n" << std::flush;

        } else if (subject == "labios.manager.workers") {
            auto all = worker_mgr.all_workers();
            std::string response;
            for (auto& w : all) {
                response += std::to_string(w.id) + ","
                    + (w.available ? "1" : "0") + ","
                    + std::to_string(w.capacity) + ","
                    + std::to_string(w.load) + ","
                    + std::to_string(w.speed) + ","
                    + std::to_string(w.energy) + "\n";
            }
            if (!reply_to.empty()) {
                nats.publish(reply_to, response);
                nats.flush();
            }
        }
    };

    nats.subscribe("labios.worker.register", handler);
    nats.subscribe("labios.worker.deregister", handler);
    nats.subscribe("labios.manager.workers", handler);

    // Score update handler: workers publish "id,capacity,load[,available]" every 2 seconds.
    nats.subscribe("labios.worker.score_update",
        [&worker_mgr](std::string_view /*subject*/,
                       std::span<const std::byte> data,
                       std::string_view /*reply_to*/) {
            std::string msg(reinterpret_cast<const char*>(data.data()), data.size());
            std::istringstream iss(msg);
            std::string token;
            int id = 0;
            double capacity = 1.0, load = 0.0;
            bool available = true;

            if (std::getline(iss, token, ',')) id = std::stoi(token);
            if (std::getline(iss, token, ',')) capacity = std::stod(token);
            if (std::getline(iss, token, ',')) load = std::stod(token);
            if (std::getline(iss, token, ',')) available = (token == "1");

            auto all = worker_mgr.all_workers();
            for (auto& w : all) {
                if (w.id == id) {
                    w.capacity = capacity;
                    w.load = load;
                    w.available = available;
                    worker_mgr.update_score(id, w);
                    break;
                }
            }
        });

    // Queue depth subscription (for elastic scaling).
    if (orchestrator) {
        nats.subscribe("labios.queue.depth",
            [&orchestrator](std::string_view /*subject*/,
                            std::span<const std::byte> data,
                            std::string_view /*reply_to*/) {
                std::string msg(reinterpret_cast<const char*>(data.data()),
                                data.size());
                try {
                    int depth = std::stoi(msg);
                    orchestrator->update_queue_depth(depth);
                } catch (...) {}
            });
    }

    redis.set("labios:ready:manager", "1");

    // Signal healthcheck
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] manager ready\n" << std::flush;

    // Start elastic orchestrator thread.
    if (orchestrator) {
        g_elastic_thread = std::jthread([&orchestrator, &nats](std::stop_token stoken) {
            while (!stoken.stop_requested()) {
                // Run one evaluation cycle.
                orchestrator->run(stoken);

                // Check for pending resume commands.
                int resume_id = orchestrator->consume_pending_resume();
                if (resume_id > 0) {
                    std::string subject = "labios.worker.resume."
                        + std::to_string(resume_id);
                    try {
                        nats.publish(subject, "resume");
                        nats.flush();
                        std::cout << "[elastic] sent resume to worker "
                                  << resume_id << "\n" << std::flush;
                    } catch (...) {}
                }
            }
        });
    }

    g_service_thread = std::jthread([](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    g_service_thread.join();

    // Stop elastic thread.
    if (g_elastic_thread.joinable()) {
        g_elastic_thread.request_stop();
        g_elastic_thread.join();
    }

    std::cout << "[" << timestamp() << "] manager shutting down\n";
    return 0;
}
