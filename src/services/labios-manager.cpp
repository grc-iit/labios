#include <labios/config.h>
#include <labios/worker_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/elastic/docker_client.h>
#include <labios/elastic/orchestrator.h>
#include <labios/worker_registry_protocol.h>

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
        if (subject == "labios.worker.register" ||
            subject == "labios.worker.deregister" ||
            subject == "labios.worker.score_update") {
            try {
                const auto message = labios::decode_worker_message(data);
                if ((subject == "labios.worker.register" &&
                     message.kind != labios::WorkerRegistryMessage::Kind::Registration) ||
                    (subject == "labios.worker.deregister" &&
                     message.kind != labios::WorkerRegistryMessage::Kind::Deregistration) ||
                    (subject == "labios.worker.score_update" &&
                     message.kind != labios::WorkerRegistryMessage::Kind::ResourceUpdate)) {
                    throw std::runtime_error("WRONG_KIND");
                }
                bool changed = false;
                if (message.kind == labios::WorkerRegistryMessage::Kind::Registration) {
                    changed = worker_mgr.register_worker_v2(message.worker);
                } else if (message.kind == labios::WorkerRegistryMessage::Kind::ResourceUpdate) {
                    changed = worker_mgr.update_worker_v2(message.worker);
                } else {
                    changed = worker_mgr.deregister_worker_v2(
                        message.worker_id, message.registration_epoch);
                }
                if (!changed) {
                    std::cerr << "[" << timestamp()
                        << "] manager: stale or duplicate registry message\n" << std::flush;
                    if (message.kind == labios::WorkerRegistryMessage::Kind::ResourceUpdate) {
                        // Unknown updates are never silently discarded: ask the
                        // live process to republish its full epoch descriptor.
                        nats.publish("labios.worker.reregister." +
                                     std::to_string(message.worker_id), "1");
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[" << timestamp() << "] manager: rejected registry message: "
                          << e.what() << "\n" << std::flush;
            }
        } else if (subject == "labios.manager.workers") {
            // Rows and generation are one locked capture; readers cannot observe
            // a generation paired with rows from a different state.
            auto [all, generation] = worker_mgr.snapshot_workers();
            auto response = labios::encode_worker_snapshot(
                all, generation,
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()));
            if (!reply_to.empty()) {
                nats.publish(reply_to, std::span<const std::byte>(response));
                nats.flush();
            }
        }
    };

    nats.subscribe("labios.worker.register", handler);
    nats.subscribe("labios.worker.deregister", handler);
    nats.subscribe("labios.worker.score_update", handler);
    nats.subscribe("labios.manager.workers", handler);

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
