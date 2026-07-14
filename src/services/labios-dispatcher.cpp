#include <labios/catalog_manager.h>
#include <labios/channel.h>
#include <labios/config.h>
#include <labios/content_manager.h>
#include <labios/continuation.h>
#include <labios/label.h>
#include <labios/observability.h>
#include <labios/shuffler.h>
#include <labios/scheduling/scheduling.h>
#include <labios/telemetry.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/uri.h>
#include <labios/worker_registry_protocol.h>

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
#include <limits>

static std::jthread g_batch_thread;
static std::jthread g_worker_refresh_thread;
static std::vector<std::jthread> g_fanout_threads;
static std::mutex g_fanout_mu;

// Cached worker list, refreshed periodically by g_worker_refresh_thread.
static std::mutex g_workers_mu;
static std::vector<labios::WorkerInfo> g_cached_workers;
static uint64_t g_registry_generation = 0;

static uint64_t now_us() {
    return labios::label_timestamp_now_us();
}

static labios::ScoreSnapshot snapshot_worker(int worker_id,
    const std::vector<labios::WorkerInfo>& workers) {
    for (auto& w : workers) {
        if (w.id == worker_id) {
            labios::ScoreSnapshot snapshot;
            snapshot.availability = w.available ? 1.0 : 0.0;
            snapshot.capacity = w.capacity;
            snapshot.load = w.load;
            snapshot.speed = static_cast<double>(w.speed);
            snapshot.energy = static_cast<double>(w.energy);
            snapshot.tier = static_cast<double>(static_cast<int>(w.tier));
            return snapshot;
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
        auto snapshot = labios::decode_worker_message(
            std::span<const std::byte>(reply.data));
        if (snapshot.kind != labios::WorkerRegistryMessage::Kind::Snapshot) {
            throw std::runtime_error("WRONG_KIND");
        }
        workers = std::move(snapshot.workers);
        {
            std::lock_guard lock(g_workers_mu);
            g_registry_generation = snapshot.registry_generation;
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
    nats.subscribe_durable("labios.labels", "dispatcher",
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view reply_to) {
            labios::LabelData label;
            try {
                label = labios::deserialize_label(data);
            } catch (const labios::LabelDecodeError& ex) {
                std::cerr << "dispatcher: rejected label (" << ex.category()
                          << "): " << ex.what() << "\n" << std::flush;
                if (!reply_to.empty()) {
                    labios::CompletionData rejection;
                    rejection.status = labios::CompletionStatus::Error;
                    rejection.error = ex.what();
                    auto payload = labios::serialize_completion(rejection);
                    nats.publish(reply_to, std::span<const std::byte>(payload));
                }
                return;
            } catch (const std::exception& ex) {
                std::cerr << "dispatcher: rejected malformed label: "
                          << ex.what() << "\n" << std::flush;
                if (!reply_to.empty()) {
                    labios::CompletionData rejection;
                    rejection.status = labios::CompletionStatus::Error;
                    rejection.error = std::string("MALFORMED_BUFFER: ") + ex.what();
                    auto payload = labios::serialize_completion(rejection);
                    nats.publish(reply_to, std::span<const std::byte>(payload));
                }
                return;
            }
            label.reply_to = std::string(reply_to);
            labios::mark_label_queued(label, now_us());
            {
                std::lock_guard lock(batch_mu);
                batch_buffer.push_back(std::move(label));
            }
            batch_cv.notify_one();
        }, cfg.nats_max_deliver,
           std::chrono::milliseconds(cfg.nats_ack_wait_ms));

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
                    auto obs = labios::handle_observe(uri, obs_workers, redis, nats, cfg, catalog);
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
                                nats.publish_durable("labios.labels",
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
            auto shuffle_time = now_us();
            for (auto& [label, /*worker_id*/ _] : result.direct_route) {
                labios::mark_label_shuffled(label, shuffle_time);
            }
            for (auto& label : result.independent) {
                labios::mark_label_shuffled(label, shuffle_time);
            }
            for (auto& st : result.supertasks) {
                labios::mark_label_shuffled(st.composite, shuffle_time);
                for (auto& child : st.children) {
                    labios::mark_label_shuffled(child, shuffle_time);
                }
            }

            // Read cached worker list (refreshed by background thread).
            std::vector<labios::WorkerInfo> workers;
            {
                std::lock_guard lock(g_workers_mu);
                workers = g_cached_workers;
            }
            // If all workers are suspended, send resume commands and re-query.
            {
                bool any_available = false;
                for (const auto& w : workers) {
                    if (w.available) { any_available = true; break; }
                }
                if (!any_available) {
                    std::cout << "[" << timestamp()
                              << "] dispatcher: all workers suspended, sending resume\n"
                              << std::flush;
                    for (const auto& w : workers) {
                        nats.publish("labios.worker.resume." + std::to_string(w.id), "1");
                    }
                    nats.flush();
                    // Wait for workers to publish their score updates through
                    // the manager. The manager processes score_update messages
                    // asynchronously, so 1s gives enough round-trip time.
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    auto refreshed = query_workers(nats);
                    std::lock_guard lock(g_workers_mu);
                    g_cached_workers = refreshed;
                    workers = std::move(refreshed);
                }
            }

            // One typed solver invocation for the complete post-shuffler batch.
            struct DispatchUnit {
                labios::SchedulingUnitDescriptor descriptor;
                labios::LabelData representative;
                std::vector<labios::LabelData> children;
                bool composite = false;
            };
            std::vector<DispatchUnit> units;
            units.reserve(result.direct_route.size() + result.independent.size() +
                          result.supertasks.size());
            uint64_t ordinal = 0;
            auto add_label_unit = [&](labios::LabelData label, int preferred_worker = 0) {
                DispatchUnit unit;
                unit.representative = std::move(label);
                unit.descriptor.unit_id = unit.representative.id;
                unit.descriptor.ordinal = ordinal;
                auto job = labios::describe_job(unit.representative, ordinal++);
                if (job) {
                    if (preferred_worker > 0) {
                        const auto domain = "worker:" + std::to_string(preferred_worker);
                        for (auto& source : job->sources) source.locality_domain = domain;
                        for (auto& destination : job->destinations) destination.locality_domain = domain;
                    }
                    unit.descriptor.members.push_back(std::move(*job));
                }
                unit.descriptor.ready = true;
                for (const auto& dependency : unit.representative.dependencies) {
                    unit.descriptor.predecessors.push_back(dependency.label_id);
                    if (catalog.get_status(dependency.label_id) != labios::LabelStatus::Complete) {
                        unit.descriptor.ready = false;
                    }
                }
                if (unit.descriptor.members.empty()) unit.descriptor.ready = false;
                for (auto& member : unit.descriptor.members) member.ready = unit.descriptor.ready;
                units.push_back(std::move(unit));
            };
            for (auto& entry : result.direct_route) add_label_unit(std::move(entry.first), entry.second);
            for (auto& label : result.independent) {
                // Materialize an aggregate before deriving its typed descriptor.
                if (!label.children.empty() && label.type == labios::LabelType::Write) {
                    std::vector<std::byte> combined;
                    if (label.data_size != 0) combined.reserve(static_cast<size_t>(label.data_size));
                    for (const auto child_id : label.children) {
                        auto chunk = redis.get_binary(labios::ContentManager::data_key(child_id));
                        combined.insert(combined.end(), chunk.begin(), chunk.end());
                        redis.del(labios::ContentManager::data_key(child_id));
                    }
                    label.data_size = static_cast<uint64_t>(combined.size());
                    redis.set_binary(labios::ContentManager::data_key(label.id),
                                     std::span<const std::byte>(combined));
                }
                int preferred_worker = 0;
                if (label.type == labios::LabelType::Write && !label.file_key.empty()) {
                    if (auto location = catalog.get_location(label.file_key)) preferred_worker = *location;
                }
                add_label_unit(std::move(label), preferred_worker);
            }
            for (auto& supertask : result.supertasks) {
                DispatchUnit unit;
                unit.composite = true;
                unit.representative = std::move(supertask.composite);
                unit.children = std::move(supertask.children);
                unit.descriptor.unit_id = unit.representative.id;
                unit.descriptor.ordinal = ordinal;
                unit.descriptor.ready = true;
                for (const auto& child : unit.children) {
                    auto job = labios::describe_job(child, ordinal++);
                    if (job) unit.descriptor.members.push_back(std::move(*job));
                    for (const auto& dependency : child.dependencies) {
                        unit.descriptor.predecessors.push_back(dependency.label_id);
                        if (catalog.get_status(dependency.label_id) != labios::LabelStatus::Complete) {
                            unit.descriptor.ready = false;
                        }
                    }
                }
                for (auto& member : unit.descriptor.members) member.ready = unit.descriptor.ready;
                units.push_back(std::move(unit));
            }

            labios::SchedulingBatch scheduling_batch;
            scheduling_batch.batch_id = now_us();
            {
                std::lock_guard lock(g_workers_mu);
                scheduling_batch.registry_generation = g_registry_generation;
            }
            for (const auto& unit : units) scheduling_batch.units.push_back(unit.descriptor);
            const auto prepared = labios::prepare_scheduling_batch(
                std::move(scheduling_batch), workers);
            auto policy_profile = profile;
            auto plan = labios::solve_prepared(prepared, cfg.scheduler_policy, policy_profile);
            const bool valid_plan = labios::validate_plan(prepared, plan);

            auto make_history = [&](const DispatchUnit& unit,
                                    const labios::PlacementDecision& decision) {
                labios::SchedulingDecisionSnapshot history;
                history.decision_id = (prepared.batch.batch_id << 1U) ^ unit.representative.id;
                history.batch_id = prepared.batch.batch_id;
                history.scheduling_unit_id = unit.descriptor.unit_id;
                history.attempt = static_cast<uint32_t>(unit.representative.score_snapshot.decisions.size() + 1U);
                history.registry_generation = prepared.batch.registry_generation;
                history.job_ordinal = unit.descriptor.ordinal;
                history.outcome = decision.outcome == labios::PlacementOutcome::Assigned ? "Assigned" : "Parked";
                history.chosen_worker_id = decision.worker_id;
                history.park_reason = decision.outcome == labios::PlacementOutcome::Assigned
                    ? std::string{} : (decision.park_reason.empty()
                        ? labios::feasibility_reason_name(decision.deferred_reason) : decision.park_reason);
                bool known = true;
                uint64_t bytes = 0;
                for (const auto& member : unit.descriptor.members) {
                    if (member.demand.kind == labios::DemandKind::Unknown) known = false;
                    if (std::numeric_limits<uint64_t>::max() - bytes < member.demand.bytes) {
                        known = false;
                        bytes = 0;
                        break;
                    }
                    bytes += member.demand.bytes;
                }
                history.reservation_bytes = bytes;
                history.complete_size_known = known;
                history.candidates = decision.candidates;
                history.policy_name = cfg.scheduler_policy;
                history.policy_evidence = decision.evidence;
                return history;
            };

            std::vector<labios::ScheduleEntry> scheduled;
            std::vector<size_t> assigned_units;
            std::cout << "[" << timestamp() << "] dispatcher: policy="
                      << cfg.scheduler_policy << ", workers=" << workers.size()
                      << ", units=" << units.size() << "\n" << std::flush;
            for (size_t index = 0; index < units.size(); ++index) {
                auto& unit = units[index];
                auto decision = index < plan.decisions.size() ? plan.decisions[index]
                    : labios::PlacementDecision{unit.descriptor.unit_id};
                if (!valid_plan) {
                    decision.outcome = labios::PlacementOutcome::Deferred;
                    decision.worker_id = -1;
                    decision.deferred_reason = labios::FeasibilityReason::NoFeasibleCurrentPlacement;
                    decision.park_reason = "INVALID_PLAN";
                }
                const auto history = make_history(unit, decision);
                auto apply = [&](labios::LabelData& label) {
                    if (decision.outcome == labios::PlacementOutcome::Assigned) {
                        auto snapshot = snapshot_worker(decision.worker_id, workers);
                        snapshot.decisions = label.score_snapshot.decisions;
                        snapshot.decision_version = 1;
                        snapshot.decisions.push_back(history);
                        labios::mark_label_scheduled(label, static_cast<uint32_t>(decision.worker_id),
                                                     cfg.scheduler_policy, snapshot, now_us());
                    } else {
                        label.score_snapshot.decision_version = 1;
                        label.score_snapshot.decisions.push_back(history);
                        label.status = labios::StatusCode::Queued;
                    }
                };
                apply(unit.representative);
                for (auto& child : unit.children) apply(child);
                if (decision.outcome == labios::PlacementOutcome::Assigned) {
                    assigned_units.push_back(index);
                    scheduled.push_back({unit.representative.id, decision.worker_id,
                                         unit.representative.flags});
                    for (const auto& child : unit.children) {
                        scheduled.push_back({child.id, decision.worker_id, child.flags});
                    }
                } else {
                    catalog.set_status(unit.representative.id, labios::LabelStatus::Parked);
                    auto parked = labios::serialize_label(unit.representative);
                    nats.publish_durable("labios.labels", parked);
                }
            }
            // Persist every validated residual and lifecycle transition before
            // publishing any worker delivery.
            catalog.schedule_batch(scheduled);
            for (const auto index : assigned_units) {
                auto& unit = units[index];
                const auto worker_id = unit.representative.routing.worker_id;
                if (unit.composite) {
                    std::vector<std::vector<std::byte>> children;
                    for (const auto& child : unit.children) children.push_back(labios::serialize_label(child));
                    auto packed = labios::pack_labels(children);
                    redis.set_binary("labios:supertask:" + std::to_string(unit.representative.id),
                                     std::span<const std::byte>(packed));
                }
                auto payload = labios::serialize_label(unit.representative);
                nats.publish_durable("labios.worker." + std::to_string(worker_id), payload);
                telemetry.record_label_dispatched();
            }
            nats.flush();
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
