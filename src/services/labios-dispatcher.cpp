#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/solver/round_robin.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

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

    // Redis constructed before NATS so it outlives NATS on destruction.
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);

    // Hardcoded worker list for M1a. M3 will replace with dynamic registry.
    std::vector<labios::WorkerInfo> workers = {
        {1, true},
        {2, true},
        {3, true},
    };

    labios::CatalogManager catalog(redis);
    labios::RoundRobinSolver solver;
    std::mutex dispatch_mutex;

    nats.subscribe("labios.labels",
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view reply_to) {
            std::lock_guard lock(dispatch_mutex);

            // Deserialize the incoming label.
            auto label = labios::deserialize_label(data);

            label.flags |= labios::LabelFlags::Scheduled;

            // Inject the NATS reply_to so the worker can respond to the client.
            label.reply_to = std::string(reply_to);

            // Re-serialize with the injected reply_to.
            auto reserialized = labios::serialize_label(label);

            // Update catalog: mark label as Scheduled.
            catalog.set_status(label.id, labios::LabelStatus::Scheduled);
            catalog.set_flags(label.id, label.flags);

            int target_worker = -1;

            // Read-locality: READs go to the worker holding the file data
            if (label.type == labios::LabelType::Read) {
                auto* src = std::get_if<labios::FilePath>(&label.source);
                if (src) {
                    auto loc = catalog.get_location(src->path);
                    if (loc.has_value()) {
                        target_worker = *loc;
                    }
                }
            }

            // Write-locality: WRITEs for a file with existing location go to same worker
            if (target_worker <= 0 && label.type == labios::LabelType::Write) {
                auto* dst = std::get_if<labios::FilePath>(&label.destination);
                if (dst) {
                    auto loc = catalog.get_location(dst->path);
                    if (loc.has_value()) {
                        target_worker = *loc;
                    }
                }
            }

            if (target_worker > 0) {
                // Direct assignment (read-locality or write-locality)
                catalog.set_worker(label.id, target_worker);
                std::string subject = "labios.worker." + std::to_string(target_worker);
                nats.publish(subject,
                             std::span<const std::byte>(reserialized.data(), reserialized.size()));
                nats.flush();

                const char* reason = (label.type == labios::LabelType::Read) ? "read" : "write";
                std::cout << "[" << timestamp() << "] dispatcher: label "
                          << label.id << " -> worker " << target_worker
                          << " (" << reason << " locality)\n" << std::flush;
            } else {
                // First assignment for this file: use solver
                std::vector<std::vector<std::byte>> label_batch;
                label_batch.push_back(std::move(reserialized));
                auto assignments = solver.assign(std::move(label_batch), workers);

                for (auto& [worker_id, assigned_labels] : assignments) {
                    catalog.set_worker(label.id, worker_id);

                    // Set location at assignment time so subsequent labels
                    // in the same async batch use write-locality. The worker
                    // also sets location at completion (confirming the mapping).
                    if (label.type == labios::LabelType::Write) {
                        auto* dst = std::get_if<labios::FilePath>(&label.destination);
                        if (dst) {
                            catalog.set_location(dst->path, worker_id);
                        }
                    }

                    for (auto& payload : assigned_labels) {
                        std::string subject = "labios.worker." + std::to_string(worker_id);
                        nats.publish(subject,
                                     std::span<const std::byte>(payload.data(), payload.size()));
                    }
                    nats.flush();

                    std::cout << "[" << timestamp() << "] dispatcher: label "
                              << label.id << " -> worker " << worker_id << "\n"
                              << std::flush;
                }
            }
        });

    redis.set("labios:ready:dispatcher", "1");

    // Signal healthcheck.
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
