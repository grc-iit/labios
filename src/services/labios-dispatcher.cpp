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

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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

static bool latest_start_expired(const labios::LabelData& label, uint64_t now) {
    if (label.ttl_seconds == 0 || label.created_us == 0) return false;
    constexpr uint64_t micros_per_second = 1'000'000ULL;
    if (label.ttl_seconds >
        (std::numeric_limits<uint64_t>::max() - label.created_us) / micros_per_second) {
        return false;
    }
    return now >= label.created_us +
        static_cast<uint64_t>(label.ttl_seconds) * micros_per_second;
}

static std::string trace_scheme_for_label(const labios::LabelData& label) {
    try {
        if (!label.dest_uri.empty()) return labios::parse_uri(label.dest_uri).scheme;
        if (!label.source_uri.empty()) return labios::parse_uri(label.source_uri).scheme;
    } catch (...) {
        // Admission already validates URIs; telemetry must never affect routing.
    }
    return "file";
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

static bool registration_may_unpark(
    const std::vector<labios::WorkerInfo>& before,
    const std::vector<labios::WorkerInfo>& after) {
    for (const auto& worker : after) {
        const auto prior = std::find_if(before.begin(), before.end(),
            [&](const auto& item) { return item.id == worker.id; });
        if (prior == before.end() || prior->registration_epoch != worker.registration_epoch)
            return true;
        if (!prior->available && worker.available) return true;
        if (prior->tier != worker.tier || prior->max_ir_version != worker.max_ir_version ||
            prior->operations != worker.operations ||
            prior->operation_versions != worker.operation_versions ||
            prior->pipeline_operations != worker.pipeline_operations ||
            prior->pipeline_operation_versions != worker.pipeline_operation_versions ||
            prior->attachments.size() != worker.attachments.size()) return true;
        for (size_t index = 0; index < worker.attachments.size(); ++index) {
            const auto& left = prior->attachments[index];
            const auto& right = worker.attachments[index];
            if (left.family != right.family || left.backend_id != right.backend_id ||
                left.scheme != right.scheme || left.locality != right.locality ||
                left.locality_domain != right.locality_domain) return true;
        }
    }
    return false;
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
        }, std::chrono::seconds(2), profile);
    telemetry.start();

    std::vector<labios::LabelData> batch_buffer;
    // Prevent a recovery scan from duplicating records already owned by this
    // process; a fresh dispatcher has an empty set and recovers them.
    std::unordered_set<uint64_t> local_handoff_ids;
    std::mutex batch_mu;
    std::condition_variable batch_cv;
    auto batch_size = static_cast<size_t>(cfg.dispatcher_batch_size);
    auto batch_timeout = std::chrono::milliseconds(cfg.dispatcher_batch_timeout_ms);

    // Observe client completion replies without changing their delivery. This
    // gives telemetry a completed-label trace while the client remains the
    // completion authority. The interval is a conservative dispatch-to-reply
    // service proxy and is never used as a correctness signal.
    nats.subscribe("_INBOX.>", [&](std::string_view /*subject*/,
                                   std::span<const std::byte> data,
                                   std::string_view /*reply_to*/) {
        try {
            const auto completion = labios::deserialize_completion(data);
            telemetry.record_attempt_completed(
                completion.label_id,
                completion.status == labios::CompletionStatus::Complete);
        } catch (...) {
            // Completion observation is best effort and must not affect delivery.
        }
    });

    // Seed the worker cache before accepting labels.
    {
        auto initial = query_workers(nats);
        std::lock_guard lock(g_workers_mu);
        g_cached_workers = std::move(initial);
    }

    // Background thread: periodically refresh the cached worker list.
    auto refresh_ms = std::chrono::milliseconds(cfg.scheduler_worker_refresh_ms);
    g_worker_refresh_thread = std::jthread(
        [&nats, &catalog, &batch_cv, refresh_ms](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(refresh_ms);
            if (stoken.stop_requested()) break;
            std::vector<labios::WorkerInfo> previous;
            {
                std::lock_guard lock(g_workers_mu);
                previous = g_cached_workers;
            }
            auto fresh = query_workers(nats);
            const bool wake_relevant = registration_may_unpark(previous, fresh);
            {
                std::lock_guard lock(g_workers_mu);
                g_cached_workers = std::move(fresh);
            }
            if (wake_relevant) {
                try {
                    const auto now = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    if (catalog.wake_parked(now) != 0) batch_cv.notify_one();
                } catch (...) {
                    // Periodic recovery remains available if wake-up fails.
                }
            }
        }
    });

    // NATS subscription: buffer incoming labels without routing.
    nats.subscribe_durable("labios.labels", "dispatcher",
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view reply_to,
            labios::transport::NatsConnection::DurableAck& ack) {
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
                ack.ack();
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
                ack.ack();
                return;
            }
            // JetStream uses the transport reply subject for consumer ACKs.
            // The producer completion inbox is sealed into the label payload
            // before durable publication and must not be overwritten here.
            labios::mark_label_queued(label, now_us());
            // This conditional catalog transaction is the durable handoff
            // boundary: canonical label and queued recovery metadata become
            // visible atomically before the JetStream message is acknowledged.
            if (!catalog.durable_handoff(label)) {
                // Redelivery after scheduling/execution: the later catalog
                // state wins and must never be regressed to queued.
                ack.ack();
                return;
            }
            bool inserted = false;
            {
                std::lock_guard lock(batch_mu);
                inserted = local_handoff_ids.insert(label.id).second;
                if (inserted) batch_buffer.push_back(std::move(label));
            }
            ack.ack();
            if (inserted) batch_cv.notify_one();
        }, cfg.nats_max_deliver,
           std::chrono::milliseconds(cfg.nats_ack_wait_ms));

    // Batch processing thread: collect -> shuffle -> dispatch.
    g_batch_thread = std::jthread([&](std::stop_token stoken) {
        auto location_lookup = [&](const std::string& file, uint64_t offset,
                                   uint64_t length) -> std::optional<int> {
            return catalog.get_location(file, offset, length);
        };

        while (!stoken.stop_requested()) {
            std::vector<uint64_t> active_batch_ids;
            try {
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
            active_batch_ids.reserve(batch.size());
            for (const auto& label : batch) active_batch_ids.push_back(label.id);
            // Catalog-backed recovery is the source of truth after a
            // dispatcher crash. It also supplies bounded parking retries;
            // no immediate republish loop is used.
            try {
                const auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                auto recovered = catalog.recoverable_labels(now_ms);
                std::lock_guard lock(batch_mu);
                for (auto& label : recovered) {
                    if (local_handoff_ids.insert(label.id).second) {
                        active_batch_ids.push_back(label.id);
                        batch.push_back(std::move(label));
                    }
                }
            } catch (const std::exception& ex) {
                std::cerr << "dispatcher: recovery scan failed: " << ex.what() << "\n";
            } catch (...) {
                std::cerr << "dispatcher: recovery scan failed\n";
            }
            // TTL expiry is an explicit terminal transition, never a parking
            // attempt limit or silent drop.
            const auto expiry_now = now_us();
            std::erase_if(batch, [&](const labios::LabelData& label) {
                if (!latest_start_expired(label, expiry_now)) return false;
                labios::CompletionData expired;
                expired.label_id = label.id;
                expired.status = labios::CompletionStatus::Error;
                expired.error = "EXPIRED: latest-start deadline passed";
                catalog.set_completion(expired);
                if (!label.reply_to.empty()) {
                    const auto payload = labios::serialize_completion(expired);
                    nats.publish(label.reply_to, std::span<const std::byte>(payload));
                }
                std::lock_guard lock(batch_mu);
                local_handoff_ids.erase(label.id);
                return true;
            });

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
                telemetry.enrich_workers(obs_workers);
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
                    catalog.set_completion(comp);
                    auto buf = labios::serialize_completion(comp);
                    if (!label.reply_to.empty()) {
                        nats.publish(label.reply_to, std::span<const std::byte>(buf));
                    }
                    std::cout << "[" << timestamp() << "] dispatcher: observe "
                              << label.id << " -> " << label.source_uri << "\n"
                              << std::flush;

                    {
                        std::lock_guard lock(batch_mu);
                        local_handoff_ids.erase(label.id);
                    }

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
            // Trace features are runtime-derived from completed labels and are
            // layered onto the immutable registry snapshot for this attempt.
            telemetry.enrich_workers(workers);
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
                std::string park_reason;
            };
            std::vector<DispatchUnit> units;
            units.reserve(result.direct_route.size() + result.independent.size() +
                          result.supertasks.size());
            uint64_t ordinal = 0;
            auto composite_readiness = [&](
                const labios::LabelData& child,
                const std::unordered_set<uint64_t>& internal_ids) {
                for (const auto& dependency : child.dependencies) {
                    if (internal_ids.contains(dependency.label_id)) continue;
                    try {
                        if (catalog.get_status(dependency.label_id) !=
                            labios::LabelStatus::Complete) {
                            return labios::DependencyReadiness{false, "BLOCKED_PREDECESSOR"};
                        }
                    } catch (...) {
                        return labios::DependencyReadiness{false, "UNKNOWN_DEPENDENCY"};
                    }
                }
                return labios::DependencyReadiness{};
            };
            auto add_label_unit = [&](labios::LabelData label, int preferred_worker = 0) {
                DispatchUnit unit;
                unit.representative = std::move(label);
                unit.descriptor.unit_id = unit.representative.id;
                unit.descriptor.ordinal = ordinal;
                unit.descriptor.ready = true;
                if (unit.representative.type == labios::LabelType::Composite) {
                    unit.composite = true;
                    for (auto& payload : catalog.get_composite_program(unit.representative.id)) {
                        try {
                            unit.children.push_back(labios::deserialize_label(payload));
                        } catch (...) {
                            unit.descriptor.ready = false;
                            unit.park_reason = "MALFORMED_COMPOSITE_PROGRAM";
                        }
                    }
                    if (unit.children.empty()) {
                        unit.descriptor.ready = false;
                        unit.park_reason = "MISSING_COMPOSITE_PROGRAM";
                    }
                    std::unordered_set<uint64_t> internal_ids;
                    for (const auto& child : unit.children) internal_ids.insert(child.id);
                    for (const auto& child : unit.children) {
                        if (auto job = labios::describe_job(child, ordinal++))
                            unit.descriptor.members.push_back(std::move(*job));
                        for (const auto& dependency : child.dependencies) {
                            if (!internal_ids.contains(dependency.label_id))
                                unit.descriptor.predecessors.push_back(dependency.label_id);
                        }
                        const auto readiness = composite_readiness(child, internal_ids);
                        if (!readiness.ready) {
                            unit.descriptor.ready = false;
                            if (unit.park_reason.empty()) unit.park_reason = readiness.reason;
                        }
                    }
                } else {
                    auto job = labios::describe_job(unit.representative, ordinal++);
                    if (job) {
                        if (preferred_worker > 0) {
                            const auto domain = "worker:" + std::to_string(preferred_worker);
                            for (auto& source : job->sources) source.locality_domain = domain;
                            for (auto& destination : job->destinations) destination.locality_domain = domain;
                        }
                        unit.descriptor.members.push_back(std::move(*job));
                    }
                    for (const auto& dependency : unit.representative.dependencies)
                        unit.descriptor.predecessors.push_back(dependency.label_id);
                    const auto readiness = catalog.dependency_readiness(unit.representative);
                    unit.descriptor.ready = readiness.ready;
                    unit.park_reason = readiness.reason;
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
                std::unordered_set<uint64_t> internal_ids;
                for (const auto& child : unit.children) internal_ids.insert(child.id);
                for (const auto& child : unit.children) {
                    auto job = labios::describe_job(child, ordinal++);
                    if (job) unit.descriptor.members.push_back(std::move(*job));
                    for (const auto& dependency : child.dependencies) {
                        if (!internal_ids.contains(dependency.label_id))
                            unit.descriptor.predecessors.push_back(dependency.label_id);
                    }
                    const auto readiness = composite_readiness(child, internal_ids);
                    if (!readiness.ready) {
                        unit.descriptor.ready = false;
                        if (unit.park_reason.empty()) unit.park_reason = readiness.reason;
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
            // Composite programs are recovery state, not an in-memory
            // optimization artifact. Persist the complete ordered child set
            // before placement, including reply metadata and child IDs.
            for (const auto& unit : units) {
                if (!unit.composite) continue;
                std::vector<std::vector<std::byte>> child_payloads;
                for (const auto& child : unit.children) {
                    catalog.persist_snapshot(child);
                    child_payloads.push_back(labios::serialize_label(child));
                }
                // The parent snapshot, child IDs, and ordered packed program
                // are the one atomic Composite commit record. Recovery derives
                // child suppression from that record, avoiding torn cross-key
                // parent links.
                catalog.persist_composite(unit.representative, child_payloads);
                {
                    std::lock_guard lock(batch_mu);
                    local_handoff_ids.insert(unit.representative.id);
                    for (const auto& child : unit.children)
                        local_handoff_ids.erase(child.id);
                }
                active_batch_ids.push_back(unit.representative.id);
            }
            const auto prepared = labios::prepare_scheduling_batch(
                std::move(scheduling_batch), workers);
            auto policy_profile = profile;
            auto plan = labios::solve_prepared(prepared, cfg.scheduler_policy, policy_profile);
            const bool valid_plan = labios::validate_plan(prepared, plan);

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
                    decision.tie_break =
                        "invalid-plan:" + decision.tie_break;
                    for (auto& candidate : decision.candidates) {
                        candidate.selected = false;
                        candidate.available_capacity_after =
                            candidate.available_capacity_before;
                    }
                    decision.minmax.final_batch_objective = 0.0;
                    for (auto& row : decision.minmax.workers) {
                        row.consumption_after = row.consumption_before;
                        row.spill_rank = 0;
                    }
                }
                if (!unit.park_reason.empty()) decision.park_reason = unit.park_reason;
                const auto attempt = static_cast<uint32_t>(
                    std::min<uint64_t>(
                        catalog.park_attempts(unit.representative.id) + 1,
                        std::numeric_limits<uint32_t>::max()));
                const auto history = labios::make_decision_snapshot(
                    prepared, index, decision, attempt,
                    cfg.scheduler_policy, policy_profile);
                auto apply = [&](labios::LabelData& label) {
                    const auto worker = std::find_if(
                        workers.begin(), workers.end(),
                        [&](const labios::WorkerInfo& candidate) {
                            return candidate.id == decision.worker_id;
                        });
                    labios::apply_scheduling_decision(
                        label, history, decision,
                        worker == workers.end() ? nullptr : &*worker,
                        cfg.scheduler_policy, now_us());
                };
                apply(unit.representative);
                for (auto& child : unit.children) apply(child);
                if (decision.outcome == labios::PlacementOutcome::Assigned) {
                    catalog.persist_snapshot(unit.representative);
                    for (const auto& child : unit.children)
                        catalog.persist_snapshot(child);
                    assigned_units.push_back(index);
                    scheduled.push_back({unit.representative.id, decision.worker_id,
                                         unit.representative.flags});
                    for (const auto& child : unit.children) {
                        scheduled.push_back({child.id, decision.worker_id, child.flags});
                    }
                } else {
                    const auto attempt = catalog.park_attempts(unit.representative.id) + 1;
                    labios::bound_decision_history(unit.representative);
                    for (auto& child : unit.children) labios::bound_decision_history(child);
                    const auto now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                    const auto delay = labios::parking_backoff_ms(attempt);
                    const auto park_reason = history.park_reason.empty()
                        ? "NO_FEASIBLE_CURRENT_PLACEMENT" : history.park_reason;
                    catalog.park(unit.representative, park_reason,
                                 attempt, now_ms + delay, history.park_reason);
                    // Composite/supertask children remain independently
                    // observable through the public completion API. Project the
                    // unit's legal deferral onto each child instead of leaving
                    // them misleadingly Queued while the parent is Parked.
                    for (const auto& child : unit.children) {
                        catalog.park(child, park_reason, attempt,
                                     now_ms + delay, history.park_reason);
                    }
                    std::lock_guard lock(batch_mu);
                    local_handoff_ids.erase(unit.representative.id);
                }
            }
            // Persist every validated residual and lifecycle transition before
            // publishing any worker delivery.
            const auto scheduled_ids = catalog.schedule_batch(scheduled);
            for (const auto index : assigned_units) {
                auto& unit = units[index];
                if (std::find(scheduled_ids.begin(), scheduled_ids.end(),
                              unit.representative.id) == scheduled_ids.end()) {
                    // Cancellation or another terminal transition won after
                    // planning. Never publish stale pre-execution work.
                    std::lock_guard lock(batch_mu);
                    local_handoff_ids.erase(unit.representative.id);
                    continue;
                }
                const auto worker_id = unit.representative.routing.worker_id;
                if (unit.composite) {
                    std::vector<std::vector<std::byte>> children;
                    for (const auto& child : unit.children)
                        children.push_back(labios::serialize_label(child));
                    catalog.persist_composite(unit.representative, children);
                }
                auto record_trace = [&](const labios::LabelData& label) {
                    const auto attempt = label.score_snapshot.decisions.empty()
                        ? 1U
                        : label.score_snapshot.decisions.back().attempt;
                    telemetry.record_attempt_dispatched({
                        label.id, attempt, static_cast<int>(worker_id),
                        trace_scheme_for_label(label), label.data_size,
                        label.priority, std::chrono::steady_clock::now()});
                };
                record_trace(unit.representative);
                for (const auto& child : unit.children) record_trace(child);
                auto payload = labios::serialize_label(unit.representative);
                nats.publish_durable("labios.worker." + std::to_string(worker_id), payload);
                std::lock_guard lock(batch_mu);
                local_handoff_ids.erase(unit.representative.id);
            }
            telemetry.expire_attempts();
            nats.flush();
            nats.flush();
            } catch (const std::exception& ex) {
                std::cerr << "dispatcher: batch failure: " << ex.what() << "\n";
                std::lock_guard lock(batch_mu);
                for (const auto id : active_batch_ids) local_handoff_ids.erase(id);
                batch_cv.notify_one();
            } catch (...) {
                std::cerr << "dispatcher: unknown batch failure\n";
                std::lock_guard lock(batch_mu);
                for (const auto id : active_batch_ids) local_handoff_ids.erase(id);
                batch_cv.notify_one();
            }
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
