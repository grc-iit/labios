#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/content_manager.h>
#include <labios/shuffler.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
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
                label = labios::deserialize_label(data);
                have_label = true;
                completion.label_id = label.id;

                std::lock_guard lock(worker_mu);

                catalog.set_status(label.id, labios::LabelStatus::Executing);
                label.flags |= labios::LabelFlags::Pending;
                catalog.set_flags(label.id, label.flags);

                if (label.type == labios::LabelType::Write) {
                    auto blob = content_manager.retrieve(label.id);

                    auto* dst = std::get_if<labios::FilePath>(&label.destination);
                    if (!dst) {
                        throw std::runtime_error(
                            "WRITE label missing FilePath destination");
                    }

                    auto full_path = storage_root / dst->path;
                    std::filesystem::create_directories(full_path.parent_path());

                    if (dst->offset > 0 &&
                        std::filesystem::exists(full_path)) {
                        std::ofstream ofs(full_path,
                            std::ios::binary | std::ios::in | std::ios::out);
                        if (!ofs) {
                            throw std::runtime_error(
                                "failed to open " + full_path.string());
                        }
                        ofs.seekp(static_cast<std::streamoff>(dst->offset));
                        ofs.write(reinterpret_cast<const char*>(blob.data()),
                                  static_cast<std::streamsize>(blob.size()));
                    } else {
                        std::ofstream ofs(full_path,
                            std::ios::binary | std::ios::out);
                        if (!ofs) {
                            throw std::runtime_error(
                                "failed to open " + full_path.string());
                        }
                        if (dst->offset > 0) {
                            ofs.seekp(
                                static_cast<std::streamoff>(dst->offset));
                        }
                        ofs.write(reinterpret_cast<const char*>(blob.data()),
                                  static_cast<std::streamsize>(blob.size()));
                    }

                    content_manager.remove(label.id);

                    // Record which worker holds this file so the dispatcher
                    // can route READs here (read-locality, paper Section 2.3).
                    catalog.set_location(dst->path, worker_id);

                    completion.status = labios::CompletionStatus::Complete;

                    std::cout << "[" << timestamp() << "] worker " << worker_id
                              << ": WRITE " << full_path.string() << " ("
                              << blob.size() << " bytes)\n" << std::flush;

                } else if (label.type == labios::LabelType::Read) {
                    auto* src = std::get_if<labios::FilePath>(&label.source);
                    if (!src) {
                        throw std::runtime_error(
                            "READ label missing FilePath source");
                    }

                    uint64_t read_size = label.data_size > 0
                        ? label.data_size
                        : src->length;

                    std::vector<std::byte> file_data;

                    // The dispatcher routes READs to the worker holding the
                    // file (read-locality, paper Section 2.3). Read from
                    // local storage directly.
                    auto full_path = storage_root / src->path;
                    std::ifstream ifs(full_path, std::ios::binary);
                    if (!ifs) {
                        throw std::runtime_error(
                            "data not found on this worker for "
                            + src->path);
                    }
                    if (src->offset > 0) {
                        ifs.seekg(static_cast<std::streamoff>(src->offset));
                    }
                    file_data.resize(read_size);
                    ifs.read(reinterpret_cast<char*>(file_data.data()),
                             static_cast<std::streamsize>(read_size));
                    file_data.resize(static_cast<size_t>(ifs.gcount()));

                    content_manager.stage(label.id,
                                    std::span<const std::byte>(file_data));
                    completion.data_key =
                        labios::ContentManager::data_key(label.id);
                    completion.status = labios::CompletionStatus::Complete;

                    std::cout << "[" << timestamp() << "] worker " << worker_id
                              << ": READ " << src->path << " ("
                              << file_data.size() << " bytes)\n" << std::flush;
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
                            auto blob = content_manager.retrieve(child.id);
                            auto* dst = std::get_if<labios::FilePath>(&child.destination);
                            if (!dst) {
                                throw std::runtime_error("WRITE child missing FilePath destination");
                            }
                            auto full_path = storage_root / dst->path;
                            std::filesystem::create_directories(full_path.parent_path());

                            if (dst->offset > 0 && std::filesystem::exists(full_path)) {
                                std::ofstream ofs(full_path,
                                    std::ios::binary | std::ios::in | std::ios::out);
                                if (!ofs) throw std::runtime_error("failed to open " + full_path.string());
                                ofs.seekp(static_cast<std::streamoff>(dst->offset));
                                ofs.write(reinterpret_cast<const char*>(blob.data()),
                                          static_cast<std::streamsize>(blob.size()));
                            } else {
                                std::ofstream ofs(full_path, std::ios::binary | std::ios::out);
                                if (!ofs) throw std::runtime_error("failed to open " + full_path.string());
                                if (dst->offset > 0) {
                                    ofs.seekp(static_cast<std::streamoff>(dst->offset));
                                }
                                ofs.write(reinterpret_cast<const char*>(blob.data()),
                                          static_cast<std::streamsize>(blob.size()));
                            }

                            content_manager.remove(child.id);
                            catalog.set_location(dst->path, worker_id);
                            child_comp.status = labios::CompletionStatus::Complete;

                            std::cout << "[" << timestamp() << "] worker " << worker_id
                                      << ":   child WRITE " << full_path.string() << " ("
                                      << blob.size() << " bytes)\n" << std::flush;

                        } else if (child.type == labios::LabelType::Read) {
                            auto* src = std::get_if<labios::FilePath>(&child.source);
                            if (!src) {
                                throw std::runtime_error("READ child missing FilePath source");
                            }
                            uint64_t read_size = child.data_size > 0 ? child.data_size : src->length;

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

                            content_manager.stage(child.id, std::span<const std::byte>(file_data));
                            child_comp.data_key = labios::ContentManager::data_key(child.id);
                            child_comp.status = labios::CompletionStatus::Complete;

                            std::cout << "[" << timestamp() << "] worker " << worker_id
                                      << ":   child READ " << src->path << " ("
                                      << file_data.size() << " bytes)\n" << std::flush;
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

            } catch (const std::exception& e) {
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

    // Signal healthcheck.
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
