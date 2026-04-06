#include <labios/catalog_manager.h>
#include <labios/channel.h>
#include <labios/config.h>
#include <labios/content_manager.h>
#include <labios/continuation.h>
#include <labios/label.h>
#include <labios/observability.h>
#include <labios/shuffler.h>
#include <labios/solver/constraint.h>
#include <labios/solver/minmax.h>
#include <labios/solver/random.h>
#include <labios/solver/round_robin.h>
#include <labios/telemetry.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/uri.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

static std::jthread g_batch_thread;
static std::jthread g_worker_refresh_thread;
static std::vector<std::jthread> g_fanout_threads;
static std::mutex g_fanout_mu;

// Cached worker list, refreshed periodically by g_worker_refresh_thread.
static std::mutex g_workers_mu;
static std::vector<labios::WorkerInfo> g_cached_workers;

static uint64_t now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

static labios::ScoreSnapshot snapshot_worker(int worker_id,
    const std::vector<labios::WorkerInfo>& workers) {
    for (auto& w : workers) {
        if (w.id == worker_id) {
            return {
                w.available ? 1.0 : 0.0,
                w.capacity,
                w.load,
                static_cast<double>(w.speed),
                static_cast<double>(w.energy),
                static_cast<double>(static_cast<int>(w.tier))
            };
        }
    }
    return {};
}

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

static std::vector<labios::WorkerInfo> query_workers(
    labios::transport::NatsConnection& nats) {
    std::vector<labios::WorkerInfo> workers;
    try {
        auto reply = nats.request("labios.manager.workers", {},
                                  std::chrono::milliseconds(2000));
        std::string data(reinterpret_cast<const char*>(reply.data.data()),
                         reply.data.size());
        std::istringstream iss(data);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            try {
                // Parse: id,available,capacity,load,speed,energy
                std::istringstream ls(line);
                std::string token;
                labios::WorkerInfo w{};
                if (std::getline(ls, token, ',')) w.id = std::stoi(token);
                if (std::getline(ls, token, ',')) w.available = (token == "1");
                if (std::getline(ls, token, ',')) w.capacity = std::stod(token);
                if (std::getline(ls, token, ',')) w.load = std::stod(token);
                if (std::getline(ls, token, ',')) w.speed = std::stoi(token);
                if (std::getline(ls, token, ',')) w.energy = std::stoi(token);
                workers.push_back(w);
            } catch (const std::exception&) {
                std::cerr << "[" << timestamp()
                          << "] dispatcher: malformed worker entry, skipping\n"
                          << std::flush;
                continue;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[" << timestamp() << "] dispatcher: manager query failed: "
                  << e.what() << "\n" << std::flush;
    }
    return workers;
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

    labios::CatalogManager catalog(redis);
    labios::ChannelRegistry channels(redis, nats);

    // Load weight profile for constraint-based solver.
    auto profile = cfg.scheduler_profile_path.empty()
        ? labios::WeightProfile{"default", 0.5, 0.0, 0.35, 0.15, 0.0}
        : labios::load_weight_profile(cfg.scheduler_profile_path);

    // Solver instances (one of these is used per batch based on config).
    labios::RoundRobinSolver rr_solver;
    labios::RandomSolver random_solver;
    labios::ConstraintSolver constraint_solver(profile);
    labios::MinMaxSolver minmax_solver;

    labios::ShufflerConfig shuf_cfg{
        .aggregation_enabled = cfg.dispatcher_aggregation_enabled,
        .dep_granularity = cfg.dispatcher_dep_granularity,
    };
    labios::Shuffler shuffler(shuf_cfg);

    // Record dispatcher start time for uptime queries.
    {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        redis.set("labios:dispatcher:start_ms", std::to_string(ms));
    }

    // Telemetry publisher streams metrics to labios.telemetry.
    labios::TelemetryPublisher telemetry(nats,
        [&]() -> std::vector<labios::WorkerInfo> {
            std::lock_guard lock(g_workers_mu);
            return g_cached_workers;
        });
    telemetry.start();

    std::vector<labios::LabelData> batch_buffer;
    std::mutex batch_mu;
    std::condition_variable batch_cv;
    auto batch_size = static_cast<size_t>(cfg.dispatcher_batch_size);
    auto batch_timeout = std::chrono::milliseconds(cfg.dispatcher_batch_timeout_ms);

    // Seed the worker cache before accepting labels.
    {
        auto initial = query_workers(nats);
        std::lock_guard lock(g_workers_mu);
        g_cached_workers = std::move(initial);
    }

    // Background thread: periodically refresh the cached worker list.
    auto refresh_ms = std::chrono::milliseconds(cfg.scheduler_worker_refresh_ms);
    g_worker_refresh_thread = std::jthread([&nats, refresh_ms](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(refresh_ms);
            if (stoken.stop_requested()) break;
            auto fresh = query_workers(nats);
            std::lock_guard lock(g_workers_mu);
            g_cached_workers = std::move(fresh);
        }
    });

    // NATS subscription: buffer incoming labels without routing.
    nats.subscribe("labios.labels",
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view reply_to) {
            auto label = labios::deserialize_label(data);
            label.reply_to = std::string(reply_to);
            label.queued_us = now_us();
            {
                std::lock_guard lock(batch_mu);
                batch_buffer.push_back(std::move(label));
            }
            batch_cv.notify_one();
        });

    // Batch processing thread: collect -> shuffle -> dispatch.
    g_batch_thread = std::jthread([&](std::stop_token stoken) {
        auto location_lookup = [&](const std::string& file, uint64_t offset,
                                   uint64_t length) -> std::optional<int> {
            return catalog.get_location(file, offset, length);
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
            // Report queue depth to manager for elastic scaling and observability.
            // Format: "total,with_pipeline,observe_count" for tier-aware decisions.
            try {
                int total = static_cast<int>(batch.size());
                int with_pipeline = 0;
                int observe_count = 0;
                for (const auto& label : batch) {
                    if (!label.pipeline.empty()) ++with_pipeline;
                    if (label.type == labios::LabelType::Observe) ++observe_count;
                }
                auto depth_str = std::to_string(total) + ","
                               + std::to_string(with_pipeline) + ","
                               + std::to_string(observe_count);
                nats.publish("labios.queue.depth", depth_str);
                redis.set("labios:queue:depth", depth_str);
            } catch (...) {}
            if (batch.empty()) continue;

            // Handle OBSERVE labels inline (no shuffling/scheduling).
            {
                std::vector<labios::WorkerInfo> obs_workers;
                {
                    std::lock_guard lock(g_workers_mu);
                    obs_workers = g_cached_workers;
                }
                std::erase_if(batch, [&](labios::LabelData& label) {
                    if (label.type != labios::LabelType::Observe) return false;
                    auto uri = labios::parse_uri(label.source_uri);
                    auto obs = labios::handle_observe(uri, obs_workers, redis, nats, cfg);
                    std::string data_key = "labios:observe:" + std::to_string(label.id);
                    redis.set(data_key, obs.json_data);
                    labios::CompletionData comp;
                    comp.label_id = label.id;
                    comp.status = obs.success ? labios::CompletionStatus::Complete
                                              : labios::CompletionStatus::Error;
                    comp.error = obs.error;
                    comp.data_key = data_key;
                    auto buf = labios::serialize_completion(comp);
                    if (!label.reply_to.empty()) {
                        nats.publish(label.reply_to, std::span<const std::byte>(buf));
                    }
                    std::cout << "[" << timestamp() << "] dispatcher: observe "
                              << label.id << " -> " << label.source_uri << "\n"
                              << std::flush;

                    // Process continuation for OBSERVE labels.
                    if (label.continuation.kind != labios::ContinuationKind::None) {
                        try {
                            auto chained = labios::process_continuation(
                                label, comp, channels, nats, redis);
                            if (chained) {
                                auto buf = labios::serialize_label(*chained);
                                nats.publish("labios.labels",
                                             std::span<const std::byte>(buf));
                            }
                        } catch (...) {}
                    }

                    return true;
                });
                if (batch.empty()) {
                    nats.flush();
                    continue;
                }
            }

            std::cout << "[" << timestamp() << "] dispatcher: processing batch of "
                      << batch.size() << " labels\n" << std::flush;

            auto result = shuffler.shuffle(std::move(batch), location_lookup);

            // Read cached worker list (refreshed by background thread).
            std::vector<labios::WorkerInfo> workers;
            {
                std::lock_guard lock(g_workers_mu);
                workers = g_cached_workers;
            }
            if (workers.empty()) {
                std::cerr << "[" << timestamp()
                          << "] dispatcher: no workers available, skipping batch\n"
                          << std::flush;
                continue;
            }

            // Solver dispatch based on configured policy.
            auto solve = [&](std::vector<std::vector<std::byte>> solver_batch)
                -> labios::AssignmentMap {
                if (cfg.scheduler_policy == "random") {
                    return random_solver.assign(std::move(solver_batch), workers);
                } else if (cfg.scheduler_policy == "constraint") {
                    return constraint_solver.assign(std::move(solver_batch), workers);
                } else if (cfg.scheduler_policy == "minmax") {
                    return minmax_solver.assign(std::move(solver_batch), workers);
                }
                return rr_solver.assign(std::move(solver_batch), workers);
            };

            std::cout << "[" << timestamp() << "] dispatcher: policy="
                      << cfg.scheduler_policy << ", workers="
                      << workers.size() << "\n" << std::flush;

            // (A) Direct-route labels (read-locality)
            if (!result.direct_route.empty()) {
                std::vector<labios::ScheduleEntry> sched;
                sched.reserve(result.direct_route.size());

                for (auto& [label, worker_id] : result.direct_route) {
                    label.flags |= labios::LabelFlags::Scheduled;
                    label.routing.worker_id = worker_id;
                    label.routing.policy = "read-locality";
                    label.dispatched_us = now_us();
                    label.score_snapshot = snapshot_worker(worker_id, workers);
                    sched.push_back({label.id, worker_id, label.flags});

                    auto serialized = labios::serialize_label(label);
                    std::string subject = "labios.worker." + std::to_string(worker_id);
                    nats.publish(subject, std::span<const std::byte>(serialized));
                    telemetry.record_label_dispatched();

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
                auto assignments = solve(std::move(solver_batch));

                for (auto& [wid, _] : assignments) {
                    std::vector<labios::ScheduleEntry> sched;
                    sched.reserve(st.children.size() + 1);

                    auto snap = snapshot_worker(wid, workers);
                    auto dispatch_time = now_us();

                    std::vector<std::vector<std::byte>> child_payloads;
                    child_payloads.reserve(st.children.size());
                    for (auto& child : st.children) {
                        child.flags |= labios::LabelFlags::Scheduled;
                        child.routing.worker_id = wid;
                        child.routing.policy = cfg.scheduler_policy;
                        child.dispatched_us = dispatch_time;
                        child.score_snapshot = snap;
                        sched.push_back({child.id, wid, child.flags});
                        child_payloads.push_back(labios::serialize_label(child));
                    }

                    auto packed = labios::pack_labels(child_payloads);
                    std::string pack_key = "labios:supertask:" + std::to_string(st.composite.id);
                    redis.set_binary(pack_key, std::span<const std::byte>(packed));

                    for (auto& child : st.children) {
                        if (child.type == labios::LabelType::Write) {
                            auto* dst = std::get_if<labios::FilePath>(&child.destination);
                            if (dst) catalog.set_location(dst->path, dst->offset, dst->length, wid);
                        }
                    }

                    st.composite.flags |= labios::LabelFlags::Scheduled;
                    st.composite.routing.worker_id = wid;
                    st.composite.routing.policy = cfg.scheduler_policy;
                    st.composite.dispatched_us = dispatch_time;
                    st.composite.score_snapshot = snap;
                    sched.push_back({st.composite.id, wid, st.composite.flags});
                    catalog.schedule_batch(sched);

                    auto serialized = labios::serialize_label(st.composite);
                    std::string subject = "labios.worker." + std::to_string(wid);
                    nats.publish(subject, std::span<const std::byte>(serialized));
                    telemetry.record_label_dispatched();

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

                        // Fan out completion to all original clients.
                        if (result.reply_fanout.count(label.id)) {
                            auto& original_replies = result.reply_fanout[label.id];
                            auto [inbox, reply_handle] = nats.create_reply_inbox();
                            label.reply_to = inbox;

                            auto fanout = std::jthread([reply_handle,
                                         replies = original_replies,
                                         &nats](std::stop_token stoken) {
                                try {
                                    auto data = reply_handle->wait(
                                        std::chrono::seconds(60));
                                    if (stoken.stop_requested()) return;
                                    for (auto& reply_to : replies) {
                                        nats.publish(reply_to,
                                            std::span<const std::byte>(data));
                                    }
                                    nats.flush();
                                } catch (...) {
                                    // Timeout; clients will timeout on their end.
                                }
                            });

                            std::lock_guard flock(g_fanout_mu);
                            // Clean up completed threads before adding new one.
                            std::erase_if(g_fanout_threads, [](std::jthread& t) {
                                return !t.joinable();
                            });
                            g_fanout_threads.push_back(std::move(fanout));
                        }
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
                        label.routing.worker_id = target_worker;
                        label.routing.policy = "write-locality";
                        label.dispatched_us = now_us();
                        label.score_snapshot = snapshot_worker(target_worker, workers);
                        sched.push_back({label.id, target_worker, label.flags});

                        auto serialized = labios::serialize_label(label);
                        std::string subject = "labios.worker." + std::to_string(target_worker);
                        nats.publish(subject, std::span<const std::byte>(serialized));
                        telemetry.record_label_dispatched();

                        std::cout << "[" << timestamp() << "] dispatcher: label "
                                  << label.id << " -> worker " << target_worker
                                  << " (write locality)\n" << std::flush;
                    } else {
                        auto serialized = labios::serialize_label(label);
                        std::vector<std::vector<std::byte>> solver_batch;
                        solver_batch.push_back(std::move(serialized));
                        auto assignments = solve(std::move(solver_batch));

                        for (auto& [wid, payloads] : assignments) {
                            label.routing.worker_id = wid;
                            label.routing.policy = cfg.scheduler_policy;
                            label.dispatched_us = now_us();
                            label.score_snapshot = snapshot_worker(wid, workers);
                            sched.push_back({label.id, wid, label.flags});

                            if (label.type == labios::LabelType::Write) {
                                auto* dst = std::get_if<labios::FilePath>(&label.destination);
                                if (dst) catalog.set_location(dst->path, dst->offset, dst->length, wid);
                            }

                            // Re-serialize with routing fields populated.
                            auto routed = labios::serialize_label(label);
                            std::string subject = "labios.worker." + std::to_string(wid);
                            nats.publish(subject, std::span<const std::byte>(routed));
                            telemetry.record_label_dispatched();

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
    telemetry.stop();

    // Stop the worker refresh thread.
    if (g_worker_refresh_thread.joinable()) {
        g_worker_refresh_thread.request_stop();
        g_worker_refresh_thread.join();
    }

    // Join all tracked fanout threads for clean shutdown.
    {
        std::lock_guard flock(g_fanout_mu);
        for (auto& t : g_fanout_threads) {
            if (t.joinable()) {
                t.request_stop();
                t.join();
            }
        }
        g_fanout_threads.clear();
    }

    std::cout << "[" << timestamp() << "] dispatcher shutting down\n";
    return 0;
}
