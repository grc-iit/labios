#include <labios/backend/posix_backend.h>
#include <labios/backend/kv_backend.h>
#include <labios/backend/sqlite_backend.h>
#include <labios/backend/registry.h>
#include <labios/catalog_manager.h>
#include <labios/channel.h>
#include <labios/config.h>
#include <labios/continuation.h>
#include <labios/label.h>
#include <labios/content_manager.h>
#include <labios/sds/executor.h>
#include <labios/sds/program_repo.h>
#include <labios/shuffler.h>
#include <labios/solver/solver.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/uri.h>
#include <labios/worker_registry_protocol.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

struct CompletionResult {
    labios::CompletionStatus status = labios::CompletionStatus::Complete;
    std::string data_key;
    std::string result_location;
    uint64_t bytes_transferred = 0;
};

static CompletionResult execute_write(
    labios::ContentManager& cm, labios::CatalogManager& cat,
    const labios::LabelData& label,
    const std::filesystem::path& storage_root, int worker_id,
    const labios::BackendRegistry& backends,
    labios::WorkerTier tier, const labios::sds::ProgramRepository& sds_repo);

static CompletionResult execute_read(
    labios::ContentManager& cm, labios::CatalogManager& cat,
    const labios::LabelData& label,
    const std::filesystem::path& storage_root, int worker_id,
    const labios::BackendRegistry& backends);

static std::jthread g_service_thread;
static std::jthread g_score_thread;
static std::atomic<int> g_active_labels{0};
static constexpr int kMaxQueueSize = 100;
static std::atomic<bool> g_suspended{false};
static std::chrono::steady_clock::time_point g_last_label_time{
    std::chrono::steady_clock::now()};

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

static uint64_t now_us() {
    return labios::label_timestamp_now_us();
}

static void publish_completion(labios::transport::NatsConnection& nats,
                               std::string_view reply_to,
                               const labios::CompletionData& completion) {
    if (reply_to.empty()) return;
    auto payload = labios::serialize_completion(completion);
    nats.publish(reply_to, std::span<const std::byte>(payload));
}

static void publish_chained_label(labios::transport::NatsConnection& nats,
                                  const labios::LabelData& label) {
    auto payload = labios::serialize_label(label);
    nats.publish_durable("labios.labels", std::span<const std::byte>(payload));
}

static void maybe_process_continuation(
    const labios::LabelData& label, const labios::CompletionData& completion,
    labios::ChannelRegistry& channels, labios::transport::NatsConnection& nats,
    labios::transport::RedisConnection& redis,
    std::string_view worker_name, std::string_view context) {
    if (label.continuation.kind == labios::ContinuationKind::None) return;
    try {
        auto chained = labios::process_continuation(
            label, completion, channels, nats, redis);
        if (chained) {
            publish_chained_label(nats, *chained);
        }
    } catch (const std::exception& ex) {
        std::cerr << "[" << timestamp() << "] " << worker_name << ": "
                  << context << " error: " << ex.what() << "\n" << std::flush;
    }
}

static void signal_handler(int /*sig*/) {
    if (g_service_thread.joinable()) {
        g_service_thread.request_stop();
    }
}

static CompletionResult execute_write(
    labios::ContentManager& cm, labios::CatalogManager& cat,
    const labios::LabelData& label,
    const std::filesystem::path& /*storage_root*/, int worker_id,
    const labios::BackendRegistry& backends,
    labios::WorkerTier tier, const labios::sds::ProgramRepository& sds_repo) {

    // Tier gating: Databot workers cannot execute pipelines.
    if (!label.pipeline.empty() && tier == labios::WorkerTier::Databot) {
        throw std::runtime_error(
            "BACKEND_UNSUPPORTED: Tier 0 (Databot) worker cannot execute labels with SDS pipelines");
    }

    std::vector<std::byte> blob;
    const bool has_staged_bytes = cm.exists(label.id);
    const bool has_declared_source = label.has_source_resource ||
        !label.source_uri.empty() ||
        !std::holds_alternative<std::monostate>(label.source);

    // A declared source is authoritative. Warehouse bytes may be used only
    // when the producer explicitly bound them as a materialization of that
    // source; otherwise the worker performs the backend read here. A write
    // without a source retains the existing direct-producer warehouse path.
    if (has_declared_source &&
        !(has_staged_bytes && label.has_input_binding &&
          label.input_binding.provenance == labios::BindingProvenance::MaterializedSource)) {
        if (label.source_uri.empty()) {
            throw std::runtime_error(
                "STAGED_CONTENT_UNAVAILABLE: declared source has no backend URI");
        }

        auto source_uri = labios::parse_uri(label.source_uri);
        auto* source_backend = backends.resolve(source_uri.scheme);
        if (!source_backend) {
            throw std::runtime_error(
                "BACKEND_UNSUPPORTED: no backend for source scheme: " +
                source_uri.scheme);
        }
        auto source_result = source_backend->get(label);
        if (!source_result.success) {
            throw std::runtime_error(
                "EXECUTION_FAILED: source read failed: " + source_result.error);
        }
        blob = std::move(source_result.data);
    } else if (has_staged_bytes) {
        blob = cm.retrieve(label.id);
    } else {
        throw std::runtime_error(
            "STAGED_CONTENT_UNAVAILABLE: write input is not staged");
    }

    // Execute SDS pipeline if present.
    if (!label.pipeline.empty()) {
        auto result = labios::sds::execute_pipeline(
            label.pipeline, std::span<const std::byte>(blob), sds_repo);
        if (!result.success) {
            throw std::runtime_error("EXECUTION_FAILED: SDS pipeline failed: " +
                                     result.error);
        }
        blob = std::move(result.data);
    }

    // URI path: resolve dest_uri through backend registry.
    if (!label.dest_uri.empty()) {
        auto uri = labios::parse_uri(label.dest_uri);
        auto* backend = backends.resolve(uri.scheme);
        if (!backend) {
            throw std::runtime_error(
                "BACKEND_UNSUPPORTED: no backend for destination scheme: " +
                uri.scheme);
        }
        auto result = backend->put(label, std::span<const std::byte>(blob));
        if (!result.success) {
            throw std::runtime_error(
                "EXECUTION_FAILED: destination write failed: " + result.error);
        }
        cm.remove(label.id);
        cat.set_location(uri.path, 0, blob.size(), worker_id);

        std::cout << "[" << timestamp() << "] worker " << worker_id
                  << ": WRITE " << label.dest_uri << " ("
                  << blob.size() << " bytes)\n" << std::flush;
        return {
            labios::CompletionStatus::Complete,
            {},
            label.dest_uri,
            static_cast<uint64_t>(blob.size())
        };
    }

    // Legacy path: use Pointer variant.
    auto* dst = std::get_if<labios::FilePath>(&label.destination);
    if (!dst) {
        throw std::runtime_error("MISSING_FIELD: WRITE label missing destination");
    }

    auto* backend = backends.resolve("file");
    if (!backend) {
        throw std::runtime_error(
            "BACKEND_UNSUPPORTED: no backend for destination scheme: file");
    }
    auto result = backend->put(label, std::span<const std::byte>(blob));
    if (!result.success) {
        throw std::runtime_error(
            "EXECUTION_FAILED: destination write failed: " + result.error);
    }

    cm.remove(label.id);
    cat.set_location(dst->path, dst->offset, dst->length, worker_id);

    std::cout << "[" << timestamp() << "] worker " << worker_id
              << ": WRITE " << dst->path << " ("
              << blob.size() << " bytes)\n" << std::flush;

    return {
        labios::CompletionStatus::Complete,
        {},
        dst->path,
        static_cast<uint64_t>(blob.size())
    };
}

static CompletionResult execute_read(
    labios::ContentManager& cm, labios::CatalogManager& /*cat*/,
    const labios::LabelData& label,
    const std::filesystem::path& /*storage_root*/, int worker_id,
    const labios::BackendRegistry& backends) {

    // URI path: resolve source_uri through backend registry.
    if (!label.source_uri.empty()) {
        auto uri = labios::parse_uri(label.source_uri);
        auto* backend = backends.resolve(uri.scheme);
        if (!backend) {
            throw std::runtime_error(
                "no backend for scheme: " + uri.scheme);
        }
        auto result = backend->get(label);
        if (!result.success) {
            throw std::runtime_error(result.error);
        }
        cm.stage(label.id, std::span<const std::byte>(result.data));

        std::cout << "[" << timestamp() << "] worker " << worker_id
                  << ": READ " << label.source_uri << " ("
                  << result.data.size() << " bytes)\n" << std::flush;
        auto data_key = labios::ContentManager::data_key(label.id);
        return {
            labios::CompletionStatus::Complete,
            data_key,
            data_key,
            static_cast<uint64_t>(result.data.size())
        };
    }

    // Legacy path: use Pointer variant.
    auto* src = std::get_if<labios::FilePath>(&label.source);
    if (!src) {
        throw std::runtime_error("READ label missing FilePath source");
    }

    uint64_t read_size = label.data_size > 0 ? label.data_size : src->length;

    auto* backend = backends.resolve("file");
    if (!backend) {
        throw std::runtime_error("no backend for scheme: file");
    }
    auto result = backend->get(label);
    if (!result.success) {
        throw std::runtime_error(result.error);
    }
    auto file_data = std::move(result.data);
    if (read_size > 0 && file_data.size() > read_size) {
        file_data.resize(static_cast<size_t>(read_size));
    }

    cm.stage(label.id, std::span<const std::byte>(file_data));

    std::cout << "[" << timestamp() << "] worker " << worker_id
              << ": READ " << src->path << " ("
              << file_data.size() << " bytes)\n" << std::flush;

    auto data_key = labios::ContentManager::data_key(label.id);
    return {
        labios::CompletionStatus::Complete,
        data_key,
        data_key,
        static_cast<uint64_t>(file_data.size())
    };
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    // Redis constructed before NATS so it outlives NATS on destruction.
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);

    labios::ContentManager content_manager(redis, cfg.label_min_size, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);

    const char* storage_env = std::getenv("LABIOS_STORAGE_ROOT");
    std::filesystem::path storage_root =
        storage_env ? storage_env : "/labios/data";
    std::filesystem::create_directories(storage_root);

    std::string worker_subject = "labios.worker." + std::to_string(cfg.worker_id);
    std::string worker_name = "worker-" + std::to_string(cfg.worker_id);

    // Backend registry for URI-based routing.
    labios::BackendRegistry backends;
    backends.register_backend(labios::PosixBackend(storage_root));

    // KV backend uses a SEPARATE Redis instance (not the warehouse).
    const char* kv_host = std::getenv("LABIOS_KV_HOST");
    const char* kv_port_str = std::getenv("LABIOS_KV_PORT");
    std::unique_ptr<labios::transport::RedisConnection> kv_redis;
    if (kv_host && kv_port_str) {
        kv_redis = std::make_unique<labios::transport::RedisConnection>(
            kv_host, std::stoi(kv_port_str));
        backends.register_backend(labios::KVBackend(*kv_redis));
    }

    // SQLite backend for structured agent memory.
    auto sqlite_path = (storage_root / "labios.db").string();
    backends.register_backend(labios::SQLiteBackend(sqlite_path));

    // SDS program repository (shared across all label executions).
    labios::sds::ProgramRepository sds_repo;
    auto worker_tier = static_cast<labios::WorkerTier>(
        std::clamp(cfg.worker_tier, 0, 2));

    labios::ChannelRegistry channels(redis, nats);

    // Mutex protects all Redis (warehouse + catalog) and file operations.
    std::mutex worker_mu;

    int worker_id = cfg.worker_id;
    const uint64_t registration_epoch = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) * 1000ULL +
        static_cast<uint64_t>(worker_id);
    labios::WorkerInfo registration;
    registration.id = worker_id;
    registration.registration_epoch = registration_epoch;
    registration.available = true;
    registration.speed = cfg.worker_speed;
    registration.energy = cfg.worker_energy;
    registration.tier = worker_tier;
    registration.max_ir_version = labios::kCurrentIrVersion;
    const auto loaded_pipeline_operations = sds_repo.list();
    registration = labios::derive_worker_capabilities(
        std::move(registration), backends, loaded_pipeline_operations);
    registration.locality_domains = {"worker:" + std::to_string(worker_id)};
    registration.total_capacity_bytes = labios::parse_size(cfg.worker_capacity);
    registration.available_capacity_bytes = registration.total_capacity_bytes;
    registration.capacity = 1.0;
    nats.subscribe_durable(worker_subject,
        "worker-" + std::to_string(worker_id),
        [&content_manager, &catalog, &nats, &redis, &worker_mu, &storage_root,
         worker_id, &backends, worker_tier, &sds_repo, &channels, &worker_name](
            std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view /*reply_to*/,
            labios::transport::NatsConnection::DurableAck& ack) {
            labios::CompletionData completion{};
            labios::LabelData label{};
            bool have_label = false;

            try {
                g_active_labels.fetch_add(1);
                g_last_label_time = std::chrono::steady_clock::now();
                if (g_suspended.load()) {
                    g_suspended.store(false);
                }
                label = labios::deserialize_label(data);
                have_label = true;
                completion.label_id = label.id;

                // Serialize claim/dedupe with live callbacks. A redelivery
                // after process death may reclaim only the direct staged core
                // Write shape whose identical bytes and destination operation
                // make replay idempotent in the currently registered adapters.
                // Other uncertain Executing operations are never reclaimed.
                std::lock_guard lock(worker_mu);
                if (auto prior = catalog.get_completion(label.id)) {
                    publish_completion(nats, label.reply_to, *prior);
                    g_active_labels.fetch_sub(1);
                    ack.ack();
                    return;
                }
                const bool replay_safe =
                    label.type == labios::LabelType::Write &&
                    label.has_input_binding &&
                    label.input_binding.provenance ==
                        labios::BindingProvenance::DirectProducer &&
                    !label.has_source_resource && label.source_uri.empty() &&
                    label.pipeline.stages.empty();
                if (!catalog.claim_execution(label.id, replay_safe)) {
                    // A pre-execution cancellation or terminal transition won.
                    // Leave an incomplete cancellation unacknowledged so its
                    // durable completion can be replayed on redelivery.
                    if (auto prior = catalog.get_completion(label.id)) {
                        publish_completion(nats, label.reply_to, *prior);
                        ack.ack();
                    }
                    g_active_labels.fetch_sub(1);
                    return;
                }
                labios::mark_label_executing(label, worker_name, now_us());
                catalog.set_flags(label.id, label.flags);

                // Deterministic live-test fixture: widen only the reproduced
                // Scheduled/Executing failure window. Unset in normal runs.
                if (const char* delay = std::getenv("LABIOS_TEST_EXECUTION_DELAY_MS")) {
                    const auto delay_ms = std::max(0, std::atoi(delay));
                    if (delay_ms != 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    }
                }

                if (label.type == labios::LabelType::Write) {
                    auto result = execute_write(
                        content_manager, catalog, label, storage_root, worker_id,
                        backends, worker_tier, sds_repo);
                    completion.status = result.status;
                    labios::mark_label_finished(
                        label, completion.status, result.result_location,
                        result.bytes_transferred);

                } else if (label.type == labios::LabelType::Read) {
                    auto result = execute_read(
                        content_manager, catalog, label, storage_root, worker_id,
                        backends);
                    completion.status = result.status;
                    completion.data_key = std::move(result.data_key);
                    labios::mark_label_finished(
                        label, completion.status, result.result_location,
                        result.bytes_transferred);

                } else if (label.type == labios::LabelType::Composite) {
                    auto child_payloads = catalog.get_composite_program(label.id);
                    if (child_payloads.empty()) {
                        throw std::runtime_error(
                            "supertask children not found for "
                            + std::to_string(label.id));
                    }
                    uint64_t composite_bytes = 0;

                    std::cout << "[" << timestamp() << "] worker " << worker_id
                              << ": SUPERTASK " << label.id << " ("
                              << child_payloads.size() << " children)\n"
                              << std::flush;

                    for (auto& payload : child_payloads) {
                        auto child = labios::deserialize_label(payload);
                        labios::CompletionData child_comp{};
                        child_comp.label_id = child.id;

                        // Composite recovery is per child. A dispatcher or
                        // worker replay resumes the ordered program without
                        // repeating an already-terminal external effect.
                        if (auto prior = catalog.get_completion(child.id)) {
                            publish_completion(nats, child.reply_to, *prior);
                            if (prior->status != labios::CompletionStatus::Complete) {
                                throw std::runtime_error(
                                    "COMPOSITE_ABORTED: prior child failed");
                            }
                            continue;
                        }

                        try {
                            labios::mark_label_executing(
                                child, worker_name, now_us());
                            catalog.set_status(child.id,
                                               labios::LabelStatus::Executing);
                            catalog.set_flags(child.id, child.flags);

                            CompletionResult child_result;
                            if (child.type == labios::LabelType::Write) {
                                child_result = execute_write(
                                    content_manager, catalog, child,
                                    storage_root, worker_id, backends,
                                    worker_tier, sds_repo);
                            } else if (child.type == labios::LabelType::Read) {
                                child_result = execute_read(
                                    content_manager, catalog, child,
                                    storage_root, worker_id, backends);
                                child_comp.data_key =
                                    std::move(child_result.data_key);
                            } else {
                                throw std::runtime_error(
                                    "unsupported child label type: "
                                    + std::to_string(
                                        static_cast<int>(child.type)));
                            }

                            child_comp.status = child_result.status;
                            composite_bytes += child_result.bytes_transferred;
                            labios::mark_label_finished(
                                child, child_comp.status,
                                child_result.result_location,
                                child_result.bytes_transferred);
                            catalog.set_status(child.id,
                                               labios::LabelStatus::Complete);
                        } catch (const std::exception& ex) {
                            child_comp.status = labios::CompletionStatus::Error;
                            child_comp.error = ex.what();
                            labios::mark_label_finished(
                                child, child_comp.status, {}, 0, ex.what());
                            catalog.set_status(child.id,
                                               labios::LabelStatus::Error);
                            catalog.set_error(child.id, ex.what());
                            catalog.set_completion(child_comp);
                            publish_completion(nats, child.reply_to, child_comp);
                            maybe_process_continuation(
                                child, child_comp, channels, nats, redis,
                                worker_name, "child continuation");
                            throw;
                        }

                        catalog.set_completion(child_comp);
                        publish_completion(nats, child.reply_to, child_comp);
                        maybe_process_continuation(
                            child, child_comp, channels, nats, redis,
                            worker_name, "child continuation");
                    }

                    completion.status = labios::CompletionStatus::Complete;
                    labios::mark_label_finished(
                        label, completion.status, {}, composite_bytes);
                    catalog.set_status(label.id, labios::LabelStatus::Complete);
                    catalog.set_completion(completion);

                    std::cout << "[" << timestamp() << "] worker " << worker_id
                              << ": SUPERTASK " << label.id << " complete\n"
                              << std::flush;

                    publish_completion(nats, label.reply_to, completion);
                    maybe_process_continuation(
                        label, completion, channels, nats, redis, worker_name,
                        "composite continuation");
                    nats.flush();
                    g_active_labels.fetch_sub(1);
                    ack.ack();
                    return;

                } else {
                    throw std::runtime_error(
                        "unsupported label type: "
                        + std::to_string(static_cast<int>(label.type)));
                }

                catalog.set_status(label.id, labios::LabelStatus::Complete);
                catalog.set_completion(completion);
                publish_completion(nats, label.reply_to, completion);
                maybe_process_continuation(
                    label, completion, channels, nats, redis, worker_name,
                    "continuation");
                nats.flush();
                g_active_labels.fetch_sub(1);
                ack.ack();

            } catch (const std::exception& e) {
                g_active_labels.fetch_sub(1);
                completion.status = labios::CompletionStatus::Error;
                completion.error = e.what();

                if (have_label) {
                    labios::mark_label_finished(
                        label, completion.status, {}, 0, e.what());
                }

                std::cerr << "[" << timestamp() << "] worker " << worker_id
                          << ": ERROR " << e.what() << "\n" << std::flush;

                try {
                    if (completion.label_id != 0) {
                        catalog.set_status(completion.label_id,
                                           labios::LabelStatus::Error);
                        catalog.set_error(completion.label_id, e.what());
                        catalog.set_completion(completion);
                    }
                } catch (...) {
                    // Best effort catalog update on error path.
                }

                if (have_label) {
                    try {
                        publish_completion(nats, label.reply_to, completion);
                        maybe_process_continuation(
                            label, completion, channels, nats, redis,
                            worker_name, "continuation");
                        nats.flush();
                    } catch (...) {
                        // The catalog is the durable boundary. If it could
                        // not be written, leave the message unacknowledged.
                    }
                    try {
                        if (completion.label_id != 0 && catalog.get_completion(completion.label_id)) {
                            ack.ack();
                        }
                    } catch (...) {}
                }
            }
        }, cfg.nats_max_deliver,
           std::chrono::milliseconds(cfg.nats_ack_wait_ms));

    redis.set("labios:ready:" + worker_name, "1");

    // Publish one verified registry-v2 registration. There is no CSV fallback.
    auto registration_payload = labios::encode_worker_registration(registration);
    nats.publish("labios.worker.register",
                 std::span<const std::byte>(registration_payload));
    nats.flush();

    // Manager-state loss is repaired both periodically and on demand.
    nats.subscribe("labios.worker.reregister." + std::to_string(cfg.worker_id),
        [&nats, &registration](std::string_view, std::span<const std::byte>,
                               std::string_view) {
            try {
                auto payload = labios::encode_worker_registration(registration);
                nats.publish("labios.worker.register", std::span<const std::byte>(payload));
                nats.flush();
            } catch (...) {}
        });

    // Subscribe to resume commands from the elastic orchestrator.
    nats.subscribe("labios.worker.resume." + std::to_string(cfg.worker_id),
        [worker_id, &worker_name, &nats, &registration](std::string_view /*subject*/,
            std::span<const std::byte> /*data*/,
            std::string_view /*reply_to*/) {
            g_suspended.store(false);
            g_last_label_time = std::chrono::steady_clock::now();
            // Immediately publish availability so the dispatcher sees us.
            auto update = registration;
            update.available = true;
            update.capacity = 1.0;
            update.load = 0.0;
            update.available_capacity_bytes = update.total_capacity_bytes;
            try {
                auto payload = labios::encode_worker_resource_update(update);
                nats.publish("labios.worker.score_update",
                             std::span<const std::byte>(payload));
                nats.flush();
            } catch (...) {}
            std::cout << "[" << timestamp() << "] " << worker_name
                      << ": resumed by manager\n" << std::flush;
        });

    // Periodic score update thread: publishes load and capacity every 2 seconds.
    g_score_thread = std::jthread([&nats, &storage_root, worker_id, cfg, &worker_name, registration](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (stoken.stop_requested()) break;

            double load = static_cast<double>(g_active_labels.load()) /
                          static_cast<double>(kMaxQueueSize);
            load = std::min(load, 1.0);

            double cap_ratio = 1.0;
            std::error_code ec;
            auto space = std::filesystem::space(storage_root, ec);
            if (!ec && space.capacity > 0) {
                cap_ratio = static_cast<double>(space.available) /
                            static_cast<double>(space.capacity);
            }

            // Self-suspend if idle beyond configured timeout.
            if (!g_suspended.load() && g_active_labels.load() == 0) {
                auto idle = std::chrono::steady_clock::now() - g_last_label_time;
                auto timeout = std::chrono::milliseconds(
                    cfg.elastic.worker_idle_timeout_ms);
                if (idle > timeout) {
                    g_suspended.store(true);
                    std::cout << "[" << timestamp() << "] " << worker_name
                              << ": self-suspending after "
                              << std::chrono::duration_cast<std::chrono::seconds>(idle).count()
                              << "s idle\n" << std::flush;
                }
            }

            bool available = !g_suspended.load();

            auto update = registration;
            update.available = available;
            update.capacity = cap_ratio;
            update.load = load;
            if (update.total_capacity_bytes != 0) {
                update.available_capacity_bytes = static_cast<uint64_t>(
                    static_cast<long double>(update.total_capacity_bytes) * cap_ratio);
            }
            try {
                // Full registration is an idempotent heartbeat and repairs a
                // manager that restarted without accepting an older epoch.
                auto payload = labios::encode_worker_registration(update);
                nats.publish("labios.worker.register",
                             std::span<const std::byte>(payload));
                nats.flush();
            } catch (...) {}
        }
    });

    // Signal healthcheck.
    { std::ofstream touch("/tmp/labios-ready"); }

    static constexpr const char* tier_names[] = {"databot", "pipeline", "agentic"};
    int tier_idx = std::clamp(cfg.worker_tier, 0, 2);
    std::cout << "[" << timestamp() << "] " << worker_name
              << " ready (speed=" << cfg.worker_speed
              << ", energy=" << cfg.worker_energy
              << ", capacity=" << cfg.worker_capacity
              << ", tier=" << tier_names[tier_idx] << ")\n"
              << std::flush;

    g_service_thread = std::jthread([](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    g_service_thread.join();

    // Stop the score update thread.
    if (g_score_thread.joinable()) {
        g_score_thread.request_stop();
        g_score_thread.join();
    }

    // Deregister by matching epoch; stale removals cannot remove a replacement.
    auto deregistration = labios::encode_worker_deregistration(worker_id, registration_epoch);
    nats.publish("labios.worker.deregister",
                 std::span<const std::byte>(deregistration));
    nats.flush();

    std::cout << "[" << timestamp() << "] " << worker_name
              << " shutting down\n";
    return 0;
}
