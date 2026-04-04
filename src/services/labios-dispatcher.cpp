#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/content_manager.h>
#include <labios/label.h>
#include <labios/shuffler.h>
#include <labios/solver/round_robin.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

static std::jthread g_batch_thread;

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
    if (g_batch_thread.joinable()) {
        g_batch_thread.request_stop();
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

    // Hardcoded worker list for M2. M3 will replace with dynamic registry.
    std::vector<labios::WorkerInfo> workers = {
        {1, true},
        {2, true},
        {3, true},
    };

    labios::CatalogManager catalog(redis);
    labios::RoundRobinSolver solver;

    labios::ShufflerConfig shuf_cfg{
        .aggregation_enabled = cfg.dispatcher_aggregation_enabled,
        .dep_granularity = cfg.dispatcher_dep_granularity,
    };
    labios::Shuffler shuffler(shuf_cfg);

    std::vector<labios::LabelData> batch_buffer;
    std::mutex batch_mu;
    std::condition_variable batch_cv;
    auto batch_size = static_cast<size_t>(cfg.dispatcher_batch_size);
    auto batch_timeout = std::chrono::milliseconds(cfg.dispatcher_batch_timeout_ms);

    // NATS subscription: buffer incoming labels without routing.
    nats.subscribe("labios.labels",
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view reply_to) {
            auto label = labios::deserialize_label(data);
            label.reply_to = std::string(reply_to);
            {
                std::lock_guard lock(batch_mu);
                batch_buffer.push_back(std::move(label));
            }
            batch_cv.notify_one();
        });

    // Batch processing thread: collect -> shuffle -> dispatch.
    g_batch_thread = std::jthread([&](std::stop_token stoken) {
        auto location_lookup = [&](const std::string& file) -> std::optional<int> {
            return catalog.get_location(file);
        };

        while (!stoken.stop_requested()) {
            std::vector<labios::LabelData> batch;
            {
                std::unique_lock lock(batch_mu);
                batch_cv.wait_for(lock, batch_timeout, [&] {
                    return stoken.stop_requested() ||
                           batch_buffer.size() >= batch_size;
                });
                if (stoken.stop_requested() && batch_buffer.empty()) break;
                batch = std::move(batch_buffer);
                batch_buffer.clear();
            }
            if (batch.empty()) continue;

            std::cout << "[" << timestamp() << "] dispatcher: processing batch of "
                      << batch.size() << " labels\n" << std::flush;

            auto result = shuffler.shuffle(std::move(batch), location_lookup);

            // (A) Direct-route labels (read-locality)
            if (!result.direct_route.empty()) {
                std::vector<labios::ScheduleEntry> sched;
                sched.reserve(result.direct_route.size());

                for (auto& [label, worker_id] : result.direct_route) {
                    label.flags |= labios::LabelFlags::Scheduled;
                    sched.push_back({label.id, worker_id, label.flags});

                    auto serialized = labios::serialize_label(label);
                    std::string subject = "labios.worker." + std::to_string(worker_id);
                    nats.publish(subject, std::span<const std::byte>(serialized));

                    std::cout << "[" << timestamp() << "] dispatcher: label "
                              << label.id << " -> worker " << worker_id
                              << " (read locality)\n" << std::flush;
                }

                catalog.schedule_batch(sched);
            }

            // (B) Supertasks
            for (auto& st : result.supertasks) {
                auto dummy = labios::serialize_label(st.composite);
                std::vector<std::vector<std::byte>> solver_batch;
                solver_batch.push_back(std::move(dummy));
                auto assignments = solver.assign(std::move(solver_batch), workers);

                for (auto& [wid, _] : assignments) {
                    std::vector<labios::ScheduleEntry> sched;
                    sched.reserve(st.children.size() + 1);

                    std::vector<std::vector<std::byte>> child_payloads;
                    child_payloads.reserve(st.children.size());
                    for (auto& child : st.children) {
                        child.flags |= labios::LabelFlags::Scheduled;
                        sched.push_back({child.id, wid, child.flags});
                        child_payloads.push_back(labios::serialize_label(child));
                    }

                    auto packed = labios::pack_labels(child_payloads);
                    std::string pack_key = "labios:supertask:" + std::to_string(st.composite.id);
                    redis.set_binary(pack_key, std::span<const std::byte>(packed));

                    for (auto& child : st.children) {
                        if (child.type == labios::LabelType::Write) {
                            auto* dst = std::get_if<labios::FilePath>(&child.destination);
                            if (dst) catalog.set_location(dst->path, wid);
                        }
                    }

                    st.composite.flags |= labios::LabelFlags::Scheduled;
                    sched.push_back({st.composite.id, wid, st.composite.flags});
                    catalog.schedule_batch(sched);

                    auto serialized = labios::serialize_label(st.composite);
                    std::string subject = "labios.worker." + std::to_string(wid);
                    nats.publish(subject, std::span<const std::byte>(serialized));

                    std::cout << "[" << timestamp() << "] dispatcher: supertask "
                              << st.composite.id << " (" << st.children.size()
                              << " children) -> worker " << wid << "\n" << std::flush;
                    break;
                }
            }

            // (C) Independent labels
            {
                std::vector<labios::ScheduleEntry> sched;
                sched.reserve(result.independent.size());

                for (auto& label : result.independent) {
                    bool aggregated = (!label.children.empty() &&
                                       label.type == labios::LabelType::Write);
                    if (aggregated) {
                        std::vector<std::byte> combined;
                        combined.reserve(label.data_size);
                        for (auto child_id : label.children) {
                            auto key = labios::ContentManager::data_key(child_id);
                            auto chunk = redis.get_binary(key);
                            combined.insert(combined.end(), chunk.begin(), chunk.end());
                            redis.del(key);
                        }
                        redis.set_binary(labios::ContentManager::data_key(label.id),
                                         std::span<const std::byte>(combined));

                        std::cout << "[" << timestamp() << "] dispatcher: aggregated "
                                  << label.children.size() << " labels for "
                                  << label.file_key << "\n" << std::flush;
                    }

                    label.flags |= labios::LabelFlags::Scheduled;

                    int target_worker = -1;
                    if (label.type == labios::LabelType::Write) {
                        auto* dst = std::get_if<labios::FilePath>(&label.destination);
                        if (dst) {
                            auto loc = catalog.get_location(dst->path);
                            if (loc.has_value()) target_worker = *loc;
                        }
                    }

                    if (target_worker > 0) {
                        sched.push_back({label.id, target_worker, label.flags});

                        auto serialized = labios::serialize_label(label);
                        std::string subject = "labios.worker." + std::to_string(target_worker);
                        nats.publish(subject, std::span<const std::byte>(serialized));

                        std::cout << "[" << timestamp() << "] dispatcher: label "
                                  << label.id << " -> worker " << target_worker
                                  << " (write locality)\n" << std::flush;
                    } else {
                        auto serialized = labios::serialize_label(label);
                        std::vector<std::vector<std::byte>> solver_batch;
                        solver_batch.push_back(std::move(serialized));
                        auto assignments = solver.assign(std::move(solver_batch), workers);

                        for (auto& [wid, payloads] : assignments) {
                            sched.push_back({label.id, wid, label.flags});

                            if (label.type == labios::LabelType::Write) {
                                auto* dst = std::get_if<labios::FilePath>(&label.destination);
                                if (dst) catalog.set_location(dst->path, wid);
                            }

                            for (auto& payload : payloads) {
                                std::string subject = "labios.worker." + std::to_string(wid);
                                nats.publish(subject, std::span<const std::byte>(payload));
                            }

                            std::cout << "[" << timestamp() << "] dispatcher: label "
                                      << label.id << " -> worker " << wid << "\n"
                                      << std::flush;
                        }
                    }
                }

                catalog.schedule_batch(sched);
            }

            nats.flush();
        }
    });

    redis.set("labios:ready:dispatcher", "1");

    // Signal healthcheck.
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] dispatcher ready (batch_size="
              << batch_size << ", timeout_ms=" << cfg.dispatcher_batch_timeout_ms
              << ")\n" << std::flush;

    g_batch_thread.join();

    std::cout << "[" << timestamp() << "] dispatcher shutting down\n";
    return 0;
}
