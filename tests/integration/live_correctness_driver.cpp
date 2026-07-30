#include <labios/client.h>
#include <labios/session.h>
#include <labios/worker_registry_protocol.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

labios::Config config() {
    labios::Config cfg;
    if (const char* value = std::getenv("LABIOS_NATS_URL")) cfg.nats_url = value;
    if (const char* value = std::getenv("LABIOS_REDIS_HOST")) cfg.redis_host = value;
    if (const char* value = std::getenv("LABIOS_REDIS_PORT")) cfg.redis_port = std::stoi(value);
    cfg.reply_timeout_ms = 120'000;
    return cfg;
}

std::vector<uint64_t> ids(std::string_view csv) {
    std::vector<uint64_t> result;
    std::stringstream input{std::string(csv)};
    std::string item;
    while (std::getline(input, item, ',')) {
        if (!item.empty()) result.push_back(std::stoull(item));
    }
    return result;
}

const char* state_name(labios::CompletionState state) {
    switch (state) {
    case labios::CompletionState::Pending: return "pending";
    case labios::CompletionState::Complete: return "complete";
    case labios::CompletionState::Failed: return "failed";
    case labios::CompletionState::Cancelled: return "cancelled";
    case labios::CompletionState::Parked: return "parked";
    case labios::CompletionState::Timeout: return "timeout";
    }
    return "unknown";
}

std::vector<std::byte> payload(size_t size, unsigned value) {
    return std::vector<std::byte>(size, static_cast<std::byte>(value & 0xffU));
}

uint64_t submit_write(labios::Client& client, std::string_view uri,
                      size_t size, unsigned value) {
    auto pending = client.async_write_to(uri, payload(size, value));
    if (pending.pending.size() != 1) throw std::runtime_error("expected one write label");
    return pending.pending.front().label_id;
}

labios::LabelData staged_child(labios::Client& client, std::string uri,
                               size_t size, unsigned value) {
    labios::LabelParams params;
    params.type = labios::LabelType::Write;
    params.dest_uri = std::move(uri);
    auto child = client.create_label(params);
    auto bytes = payload(size, value);
    client.session().content_manager().stage(child.id, bytes);
    child.data_size = bytes.size();
    child.has_input_binding = true;
    child.input_binding.provenance = labios::BindingProvenance::DirectProducer;
    child.input_binding.content_id = std::to_string(child.id);
    child.input_binding.logical_length = bytes.size();
    labios::normalize_label_resources(child);
    client.session().catalog_manager().admit(child);
    return child;
}

uint64_t submit_composite(labios::Client& client, std::string_view prefix) {
    auto first = staged_child(client, std::string(prefix) + "-a.bin", 1024, 0xa1);
    auto second = staged_child(client, std::string(prefix) + "-b.bin", 1024, 0xb2);
    labios::LabelParams params;
    params.type = labios::LabelType::Composite;
    auto parent = client.create_label(params);
    parent.children = {first.id, second.id};
    std::vector<std::vector<std::byte>> program{
        labios::serialize_label(first), labios::serialize_label(second)};
    client.session().catalog_manager().persist_composite(parent, program);
    auto pending = client.publish(parent);
    std::cout << "CHILD_IDS=" << first.id << ',' << second.id << '\n';
    return pending.pending.front().label_id;
}

void print_wait(labios::Client& client, std::span<const uint64_t> label_ids,
                std::chrono::milliseconds timeout, std::string_view expected) {
    const auto result = client.wait_all(label_ids, timeout);
    bool okay = true;
    for (const auto& item : result.results) {
        std::cout << "OUTCOME=" << item.label_id << ':' << state_name(item.state)
                  << ':' << item.error << '\n';
        if (expected == "terminal") okay = okay && item.terminal();
        else okay = okay && state_name(item.state) == expected;
    }
    if (!okay || result.results.size() != label_ids.size()) {
        throw std::runtime_error("unexpected completion outcome");
    }
}

} // namespace

int main(int argc, char** argv) try {
    if (argc < 2) throw std::runtime_error("missing command");
    const std::string command = argv[1];
    auto client = labios::connect(config());

    if (command == "pipeline") {
        if (argc != 3) throw std::runtime_error("pipeline RUN_ID");
        const std::string source = "file:///live/" + std::string(argv[2]) + "-source.bin";
        const std::string destination = "sqlite:///live/" + std::string(argv[2]) + "-result";
        const uint64_t values[] = {50, 10, 40, 20, 30};
        std::vector<std::byte> bytes(sizeof(values));
        std::memcpy(bytes.data(), values, sizeof(values));
        client.write_to(source, bytes);
        labios::sds::Pipeline pipeline;
        pipeline.stages.push_back({"builtin://sort_uint64", "", -1, 1});
        pipeline.stages.push_back({"builtin://truncate", std::to_string(2 * sizeof(uint64_t)), 0, -1});
        auto pending = client.execute_pipeline(source, destination, pipeline);
        const auto id = pending.pending.front().label_id;
        client.wait(pending);
        const auto actual = client.read_from(destination, 2 * sizeof(uint64_t));
        if (actual.size() != 2 * sizeof(uint64_t) ||
            std::memcmp(actual.data(), values + 1, sizeof(uint64_t)) != 0 ||
            std::memcmp(actual.data() + sizeof(uint64_t), values + 3, sizeof(uint64_t)) != 0) {
            throw std::runtime_error("pipeline destination bytes mismatch");
        }
        std::cout << "ID=" << id << "\nDATA_CHECK=sorted-truncated-bytes-ok\n";
    } else if (command == "submit-write") {
        if (argc != 5) throw std::runtime_error("submit-write URI SIZE BYTE");
        std::cout << "ID=" << submit_write(client, argv[2], std::stoull(argv[3]), std::stoul(argv[4])) << '\n';
    } else if (command == "submit-read") {
        if (argc != 4) throw std::runtime_error("submit-read URI SIZE");
        auto pending = client.async_read_from(argv[2], std::stoull(argv[3]));
        std::cout << "ID=" << pending.pending.front().label_id << '\n';
    } else if (command == "submit-batch") {
        if (argc != 6) throw std::runtime_error("submit-batch URI_PREFIX COUNT SIZE BYTE");
        const auto count = std::stoi(argv[3]);
        for (int index = 0; index < count; ++index) {
            const auto uri = std::string(argv[2]) + '-' + std::to_string(index) + ".bin";
            std::cout << "ITEM=" << index << ':'
                      << submit_write(client, uri, std::stoull(argv[4]), std::stoul(argv[5]))
                      << ':' << uri << '\n';
        }
    } else if (command == "prepare-write") {
        if (argc != 5) throw std::runtime_error("prepare-write URI SIZE BYTE");
        const auto label = staged_child(client, argv[2], std::stoull(argv[3]), std::stoul(argv[4]));
        // Deterministic external-predecessor fixture: keep this record out of
        // dispatcher recovery until publish-snapshot explicitly activates it.
        client.session().catalog_manager().set_status(label.id, labios::LabelStatus::Executing);
        std::cout << "ID=" << label.id << '\n';
    } else if (command == "publish-snapshot") {
        if (argc != 3) throw std::runtime_error("publish-snapshot ID");
        const auto id = std::stoull(argv[2]);
        const auto snapshot = client.session().catalog_manager().get_snapshot(id);
        if (!snapshot) throw std::runtime_error("prepared snapshot missing");
        client.session().catalog_manager().set_status(id, labios::LabelStatus::Queued);
        const auto pending = client.publish(*snapshot);
        std::cout << "ID=" << pending.pending.front().label_id << '\n';
    } else if (command == "submit-dependent") {
        if (argc != 6) throw std::runtime_error("submit-dependent URI PREDECESSOR SIZE BYTE");
        labios::LabelParams params;
        params.type = labios::LabelType::Write;
        params.dest_uri = argv[2];
        params.declared_dependencies.push_back(std::stoull(argv[3]));
        auto label = client.create_label(params);
        label.data_size = std::stoull(argv[4]);
        auto pending = client.publish(label, payload(label.data_size, std::stoul(argv[5])));
        std::cout << "ID=" << pending.pending.front().label_id << '\n';
    } else if (command == "submit-pipeline") {
        if (argc != 4) throw std::runtime_error("submit-pipeline SOURCE DESTINATION");
        labios::sds::Pipeline pipeline;
        pipeline.stages.push_back({"builtin://identity", "", -1, 1});
        auto pending = client.execute_pipeline(argv[2], argv[3], pipeline);
        std::cout << "ID=" << pending.pending.front().label_id << '\n';
    } else if (command == "submit-composite") {
        if (argc != 3) throw std::runtime_error("submit-composite URI_PREFIX");
        const auto id = submit_composite(client, argv[2]);
        std::cout << "ID=" << id << '\n';
    } else if (command == "wait") {
        if (argc < 3 || argc > 5) throw std::runtime_error("wait IDS [EXPECTED] [TIMEOUT_MS]");
        const auto label_ids = ids(argv[2]);
        print_wait(client, label_ids,
                   std::chrono::milliseconds(argc == 5 ? std::stoll(argv[4]) : 120'000),
                   argc >= 4 ? argv[3] : "complete");
    } else if (command == "state") {
        if (argc != 3) throw std::runtime_error("state ID");
        labios::PendingIO pending{{{std::stoull(argv[2]), {}, {}}}};
        const auto result = client.test(pending);
        std::cout << "STATE=" << state_name(result.state) << "\nERROR=" << result.error << '\n';
    } else if (command == "cancel") {
        if (argc != 3) throw std::runtime_error("cancel ID");
        const auto id = std::stoull(argv[2]);
        std::cout << "CANCEL_WON=" << (client.cancel(id) ? 1 : 0) << '\n';
        const std::array<uint64_t, 1> one{id};
        print_wait(client, one, std::chrono::seconds(120), "terminal");
    } else if (command == "verify") {
        if (argc != 5) throw std::runtime_error("verify URI SIZE BYTE");
        const auto expected = payload(std::stoull(argv[3]), std::stoul(argv[4]));
        const auto actual = client.read_from(argv[2], expected.size());
        if (actual != expected) throw std::runtime_error("result bytes mismatch");
        std::cout << "DATA_CHECK=ok\n";
    } else if (command == "verify-result") {
        if (argc != 5) throw std::runtime_error("verify-result ID SIZE BYTE");
        const auto completion = client.session().catalog_manager().get_completion(std::stoull(argv[2]));
        if (!completion || completion->data_key.empty()) throw std::runtime_error("read result binding missing");
        const auto actual = client.session().content_manager().retrieve_key(completion->data_key);
        if (actual != payload(std::stoull(argv[3]), std::stoul(argv[4]))) {
            throw std::runtime_error("read completion bytes mismatch");
        }
        std::cout << "DATA_CHECK=result-ok\n";
    } else if (command == "inspect") {
        if (argc != 3) throw std::runtime_error("inspect IDS");
        for (const auto id : ids(argv[2])) {
            const auto snapshot = client.session().catalog_manager().get_snapshot(id);
            const auto completion = client.session().catalog_manager().get_completion(id);
            std::cout << "RECORD=" << id << ":status="
                      << labios::to_string(client.session().catalog_manager().get_status(id))
                      << ":completion=" << (completion ? 1 : 0)
                      << ":decisions=" << (snapshot ? snapshot->score_snapshot.decisions.size() : 0)
                      << '\n';
            if (snapshot) {
                for (const auto& decision : snapshot->score_snapshot.decisions) {
                    std::cout << "DECISION=" << id << ':' << decision.attempt << ':'
                              << decision.outcome << ':' << decision.park_reason << ':'
                              << decision.chosen_worker_id << ':' << decision.candidates.size() << '\n';
                }
            }
        }
    } else if (command == "replay") {
        if (argc != 3) throw std::runtime_error("replay ID");
        const auto id = std::stoull(argv[2]);
        const auto snapshot = client.session().catalog_manager().get_snapshot(id);
        if (!snapshot) throw std::runtime_error("snapshot missing");
        const auto bytes = labios::serialize_label(*snapshot);
        client.session().nats().publish_durable("labios.worker." + std::to_string(snapshot->routing.worker_id), bytes);
        client.session().nats().flush();
        std::cout << "REPLAYED=" << id << '\n';
    } else if (command == "registry") {
        auto reply = client.session().nats().request("labios.manager.workers", {}, std::chrono::seconds(5));
        const auto registry = labios::decode_worker_message(reply.data);
        std::cout << "GENERATION=" << registry.registry_generation << '\n';
        for (const auto& worker : registry.workers) {
            std::cout << "WORKER=" << worker.id << ':' << worker.registration_epoch << ':'
                      << (worker.available ? 1 : 0) << ':' << worker.available_capacity_bytes << '\n';
        }
    } else if (command == "deregister") {
        if (argc != 4) throw std::runtime_error("deregister WORKER EPOCH");
        const auto bytes = labios::encode_worker_deregistration(std::stoi(argv[2]), std::stoull(argv[3]));
        client.session().nats().publish("labios.worker.deregister", bytes);
        client.session().nats().flush();
        std::cout << "DEREGISTERED_MESSAGE=sent\n";
    } else {
        throw std::runtime_error("unknown command: " + command);
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "live-correctness-driver: " << error.what() << '\n';
    return 2;
}
