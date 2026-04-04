#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/content_manager.h>
#include <labios/shuffler.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

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
};

static CompletionResult execute_write(
    labios::ContentManager& cm, labios::CatalogManager& cat,
    const labios::LabelData& label,
    const std::filesystem::path& storage_root, int worker_id);

static CompletionResult execute_read(
    labios::ContentManager& cm, labios::CatalogManager& cat,
    const labios::LabelData& label,
    const std::filesystem::path& storage_root, int worker_id);

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

static void signal_handler(int /*sig*/) {
    if (g_service_thread.joinable()) {
        g_service_thread.request_stop();
    }
}

static CompletionResult execute_write(
    labios::ContentManager& cm, labios::CatalogManager& cat,
    const labios::LabelData& label,
    const std::filesystem::path& storage_root, int worker_id) {

    auto blob = cm.retrieve(label.id);

    auto* dst = std::get_if<labios::FilePath>(&label.destination);
    if (!dst) {
        throw std::runtime_error("WRITE label missing FilePath destination");
    }

    auto full_path = storage_root / dst->path;
    std::filesystem::create_directories(full_path.parent_path());

    if (dst->offset > 0 && std::filesystem::exists(full_path)) {
        std::ofstream ofs(full_path,
            std::ios::binary | std::ios::in | std::ios::out);
        if (!ofs) {
            throw std::runtime_error("failed to open " + full_path.string());
        }
        ofs.seekp(static_cast<std::streamoff>(dst->offset));
        ofs.write(reinterpret_cast<const char*>(blob.data()),
                  static_cast<std::streamsize>(blob.size()));
    } else {
        std::ofstream ofs(full_path, std::ios::binary | std::ios::out);
        if (!ofs) {
            throw std::runtime_error("failed to open " + full_path.string());
        }
        if (dst->offset > 0) {
            ofs.seekp(static_cast<std::streamoff>(dst->offset));
        }
        ofs.write(reinterpret_cast<const char*>(blob.data()),
                  static_cast<std::streamsize>(blob.size()));
    }

    cm.remove(label.id);
    cat.set_location(dst->path, dst->offset, dst->length, worker_id);

    std::cout << "[" << timestamp() << "] worker " << worker_id
              << ": WRITE " << full_path.string() << " ("
              << blob.size() << " bytes)\n" << std::flush;

    return {labios::CompletionStatus::Complete, {}};
}

static CompletionResult execute_read(
    labios::ContentManager& cm, labios::CatalogManager& /*cat*/,
    const labios::LabelData& label,
    const std::filesystem::path& storage_root, int worker_id) {

    auto* src = std::get_if<labios::FilePath>(&label.source);
    if (!src) {
        throw std::runtime_error("READ label missing FilePath source");
    }

    uint64_t read_size = label.data_size > 0 ? label.data_size : src->length;

    auto full_path = storage_root / src->path;
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error(
            "data not found on this worker for " + src->path);
    }
    if (src->offset > 0) {
        ifs.seekg(static_cast<std::streamoff>(src->offset));
    }
    std::vector<std::byte> file_data(read_size);
    ifs.read(reinterpret_cast<char*>(file_data.data()),
             static_cast<std::streamsize>(read_size));
    file_data.resize(static_cast<size_t>(ifs.gcount()));

    cm.stage(label.id, std::span<const std::byte>(file_data));

    std::cout << "[" << timestamp() << "] worker " << worker_id
              << ": READ " << src->path << " ("
              << file_data.size() << " bytes)\n" << std::flush;

    return {labios::CompletionStatus::Complete,
            labios::ContentManager::data_key(label.id)};
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

    // Mutex protects all Redis (warehouse + catalog) and file operations.
    std::mutex worker_mu;

    int worker_id = cfg.worker_id;
    nats.subscribe(worker_subject,
        [&content_manager, &catalog, &nats, &redis, &worker_mu, &storage_root, worker_id](
            std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view /*reply_to*/) {
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

                std::lock_guard lock(worker_mu);

                catalog.set_status(label.id, labios::LabelStatus::Executing);
                label.flags |= labios::LabelFlags::Pending;
                catalog.set_flags(label.id, label.flags);

                if (label.type == labios::LabelType::Write) {
                    auto result = execute_write(
                        content_manager, catalog, label, storage_root, worker_id);
                    completion.status = result.status;

                } else if (label.type == labios::LabelType::Read) {
                    auto result = execute_read(
                        content_manager, catalog, label, storage_root, worker_id);
                    completion.status = result.status;
                    completion.data_key = std::move(result.data_key);

                } else if (label.type == labios::LabelType::Composite) {
                    // Supertask: retrieve packed children from Redis and execute in order.
                    std::string pack_key = "labios:supertask:" + std::to_string(label.id);
                    auto packed = redis.get_binary(pack_key);
                    if (packed.empty()) {
                        throw std::runtime_error(
                            "supertask children not found for " + std::to_string(label.id));
                    }

                    auto child_payloads = labios::unpack_labels(packed);

                    std::cout << "[" << timestamp() << "] worker " << worker_id
                              << ": SUPERTASK " << label.id << " ("
                              << child_payloads.size() << " children)\n" << std::flush;

                    for (auto& payload : child_payloads) {
                        auto child = labios::deserialize_label(payload);
                        labios::CompletionData child_comp{};
                        child_comp.label_id = child.id;

                        catalog.set_status(child.id, labios::LabelStatus::Executing);
                        child.flags |= labios::LabelFlags::Pending;
                        catalog.set_flags(child.id, child.flags);

                        if (child.type == labios::LabelType::Write) {
                            auto result = execute_write(
                                content_manager, catalog, child, storage_root, worker_id);
                            child_comp.status = result.status;

                        } else if (child.type == labios::LabelType::Read) {
                            auto result = execute_read(
                                content_manager, catalog, child, storage_root, worker_id);
                            child_comp.status = result.status;
                            child_comp.data_key = std::move(result.data_key);

                        } else {
                            throw std::runtime_error(
                                "unsupported child label type: "
                                + std::to_string(static_cast<int>(child.type)));
                        }

                        catalog.set_status(child.id, labios::LabelStatus::Complete);

                        // Send completion for each child individually.
                        if (!child.reply_to.empty()) {
                            auto reply = labios::serialize_completion(child_comp);
                            nats.publish(child.reply_to, std::span<const std::byte>(reply));
                        }
                    }

                    // Clean up packed data from Redis.
                    redis.del(pack_key);

                    // Mark composite complete.
                    completion.status = labios::CompletionStatus::Complete;
                    catalog.set_status(label.id, labios::LabelStatus::Complete);

                    std::cout << "[" << timestamp() << "] worker " << worker_id
                              << ": SUPERTASK " << label.id << " complete\n" << std::flush;

                    // The composite's own reply_to is typically empty (no client waits on it).
                    if (!label.reply_to.empty()) {
                        auto reply_payload = labios::serialize_completion(completion);
                        nats.publish(label.reply_to, std::span<const std::byte>(reply_payload));
                    }
                    nats.flush();
                    g_active_labels.fetch_sub(1);
                    return; // Early return; skip the normal completion path below.

                } else {
                    throw std::runtime_error(
                        "unsupported label type: " +
                        std::to_string(static_cast<int>(label.type)));
                }

                catalog.set_status(label.id, labios::LabelStatus::Complete);

                if (!label.reply_to.empty()) {
                    auto reply_payload = labios::serialize_completion(completion);
                    nats.publish(label.reply_to,
                                 std::span<const std::byte>(reply_payload));
                    nats.flush();
                }

                g_active_labels.fetch_sub(1);

            } catch (const std::exception& e) {
                g_active_labels.fetch_sub(1);
                completion.status = labios::CompletionStatus::Error;
                completion.error = e.what();

                std::cerr << "[" << timestamp() << "] worker " << worker_id
                          << ": ERROR " << e.what() << "\n" << std::flush;

                try {
                    if (completion.label_id != 0) {
                        catalog.set_status(completion.label_id,
                                           labios::LabelStatus::Error);
                        catalog.set_error(completion.label_id, e.what());
                    }
                } catch (...) {
                    // Best effort catalog update on error path.
                }

                if (have_label && !label.reply_to.empty()) {
                    try {
                        auto reply_payload = labios::serialize_completion(completion);
                        nats.publish(label.reply_to,
                                     std::span<const std::byte>(reply_payload));
                        nats.flush();
                    } catch (...) {
                        // Best effort completion notification on error path.
                    }
                }
            }
        });

    redis.set("labios:ready:" + worker_name, "1");

    // Publish registration to manager.
    std::string reg_msg = std::to_string(cfg.worker_id) + ","
        + std::to_string(cfg.worker_speed) + ","
        + std::to_string(cfg.worker_energy) + ","
        + cfg.worker_capacity;
    nats.publish("labios.worker.register", reg_msg);
    nats.flush();

    // Subscribe to resume commands from the elastic orchestrator.
    nats.subscribe("labios.worker.resume." + std::to_string(cfg.worker_id),
        [worker_id, &worker_name](std::string_view /*subject*/,
            std::span<const std::byte> /*data*/,
            std::string_view /*reply_to*/) {
            g_suspended.store(false);
            g_last_label_time = std::chrono::steady_clock::now();
            std::cout << "[" << timestamp() << "] " << worker_name
                      << ": resumed by manager\n" << std::flush;
        });

    // Periodic score update thread: publishes load and capacity every 2 seconds.
    g_score_thread = std::jthread([&nats, &storage_root, worker_id, cfg, &worker_name](std::stop_token stoken) {
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

            // Format: "id,capacity,load,available"
            std::string msg = std::to_string(worker_id) + ","
                + std::to_string(cap_ratio) + ","
                + std::to_string(load) + ","
                + (available ? "1" : "0");
            try {
                nats.publish("labios.worker.score_update", msg);
                nats.flush();
            } catch (...) {}
        }
    });

    // Signal healthcheck.
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] " << worker_name
              << " ready (speed=" << cfg.worker_speed
              << ", energy=" << cfg.worker_energy
              << ", capacity=" << cfg.worker_capacity << ")\n"
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

    // Deregister from manager before shutdown.
    nats.publish("labios.worker.deregister", std::to_string(cfg.worker_id));
    nats.flush();

    std::cout << "[" << timestamp() << "] " << worker_name
              << " shutting down\n";
    return 0;
}
