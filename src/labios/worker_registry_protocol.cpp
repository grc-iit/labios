#include <labios/worker_registry_protocol.h>
#include <labios/backend/registry.h>
#include <labios/label.h>

#include "worker_registry_generated.h"
#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace labios {
namespace wire {
using namespace registry;

[[noreturn]] void invalid(const char* category) {
    throw std::runtime_error(category);
}

void validate_worker(const WorkerInfo& worker) {
    if (worker.id <= 0 || worker.registration_epoch == 0) invalid("INVALID_RANGE");
    if (!std::isfinite(worker.capacity) || worker.capacity < 0.0 || worker.capacity > 1.0 ||
        !std::isfinite(worker.load) || worker.load < 0.0 || worker.load > 1.0 ||
        !std::isfinite(worker.skills) || worker.skills < 0.0 || worker.skills > 1.0 ||
        !std::isfinite(worker.compute) || worker.compute < 0.0 || worker.compute > 1.0 ||
        worker.speed < 1 || worker.speed > 5 || worker.energy < 1 || worker.energy > 5 ||
        static_cast<uint8_t>(worker.tier) > static_cast<uint8_t>(WorkerTier::Agentic) ||
        worker.max_ir_version == 0 ||
        (worker.total_capacity_bytes != 0 &&
         worker.available_capacity_bytes > worker.total_capacity_bytes)) {
        invalid("INVALID_RANGE");
    }
    std::set<std::string> operations(worker.operations.begin(), worker.operations.end());
    std::set<std::string> pipeline(worker.pipeline_operations.begin(), worker.pipeline_operations.end());
    if (operations.size() != worker.operations.size() || pipeline.size() != worker.pipeline_operations.size() ||
        worker.operation_versions.size() != worker.operations.size() ||
        worker.pipeline_operation_versions.size() != worker.pipeline_operations.size() ||
        std::any_of(worker.operation_versions.begin(), worker.operation_versions.end(),
                    [](uint32_t version) { return version == 0; }) ||
        std::any_of(worker.pipeline_operation_versions.begin(), worker.pipeline_operation_versions.end(),
                    [](uint32_t version) { return version == 0; })) {
        invalid("INCONSISTENT_CAPABILITY");
    }
    std::set<std::string> attachments;
    for (const auto& attachment : worker.attachments) {
        if (attachment.family > static_cast<uint8_t>(ResourceFamily::Extension) ||
            attachment.backend_id.empty() || attachment.scheme.empty() ||
            static_cast<uint8_t>(attachment.locality) > static_cast<uint8_t>(LocalityKind::Hard) ||
            (attachment.locality == LocalityKind::Hard && attachment.locality_domain.empty())) {
            invalid("INCONSISTENT_ATTACHMENT");
        }
        const auto key = std::to_string(attachment.family) + "\n" + attachment.backend_id +
                         "\n" + attachment.scheme;
        if (!attachments.insert(key).second) invalid("INCONSISTENT_ATTACHMENT");
    }
}

flatbuffers::Offset<WorkerDescriptor> make_worker(flatbuffers::FlatBufferBuilder& builder,
                                                   const WorkerInfo& worker) {
    validate_worker(worker);
    std::vector<flatbuffers::Offset<flatbuffers::String>> operations;
    std::vector<flatbuffers::Offset<flatbuffers::String>> pipeline;
    std::vector<uint32_t> operation_versions = worker.operation_versions;
    std::vector<uint32_t> pipeline_operation_versions = worker.pipeline_operation_versions;
    std::vector<flatbuffers::Offset<flatbuffers::String>> domains;
    for (const auto& operation : worker.operations) operations.push_back(builder.CreateString(operation));
    for (const auto& operation : worker.pipeline_operations) pipeline.push_back(builder.CreateString(operation));
    for (const auto& domain : worker.locality_domains) domains.push_back(builder.CreateString(domain));
    std::vector<flatbuffers::Offset<Attachment>> attachments;
    for (const auto& attachment : worker.attachments) {
        attachments.push_back(CreateAttachment(
            builder, attachment.family, builder.CreateString(attachment.backend_id),
            builder.CreateString(attachment.scheme), static_cast<uint8_t>(attachment.locality),
            builder.CreateString(attachment.locality_domain)));
    }
    return CreateWorkerDescriptor(
        builder, worker.id, worker.registration_epoch, worker.registry_generation,
        worker.available, worker.total_capacity_bytes, worker.available_capacity_bytes,
        worker.capacity, worker.load, static_cast<uint8_t>(worker.speed),
        static_cast<uint8_t>(worker.energy), static_cast<uint8_t>(worker.tier),
        worker.max_ir_version, builder.CreateVector(operations), builder.CreateVector(pipeline),
        builder.CreateVector(attachments), builder.CreateVector(domains), worker.skills,
        worker.compute, static_cast<uint8_t>(worker.reasoning),
        builder.CreateVector(operation_versions),
        builder.CreateVector(pipeline_operation_versions));
}

WorkerInfo read_worker(const WorkerDescriptor* descriptor) {
    if (descriptor == nullptr) invalid("MALFORMED_BUFFER");
    WorkerInfo worker;
    worker.id = descriptor->id();
    worker.registration_epoch = descriptor->registration_epoch();
    worker.registry_generation = descriptor->registry_generation();
    worker.available = descriptor->available();
    worker.total_capacity_bytes = descriptor->total_capacity_bytes();
    worker.available_capacity_bytes = descriptor->available_capacity_bytes();
    worker.capacity = descriptor->capacity();
    worker.load = descriptor->load();
    worker.speed = descriptor->speed();
    worker.energy = descriptor->energy();
    worker.tier = static_cast<WorkerTier>(descriptor->tier());
    worker.max_ir_version = descriptor->max_ir_version();
    worker.skills = descriptor->skills();
    worker.compute = descriptor->compute();
    worker.reasoning = descriptor->reasoning();
    if (const auto* values = descriptor->operations()) {
        for (const auto* value : *values) if (value) worker.operations.emplace_back(value->str());
    }
    if (const auto* values = descriptor->operation_versions()) {
        for (auto value : *values) worker.operation_versions.push_back(value);
    }
    if (const auto* values = descriptor->pipeline_operations()) {
        for (const auto* value : *values) if (value) worker.pipeline_operations.emplace_back(value->str());
    }
    if (const auto* values = descriptor->pipeline_operation_versions()) {
        for (auto value : *values) worker.pipeline_operation_versions.push_back(value);
    }
    if (const auto* values = descriptor->locality_domains()) {
        for (const auto* value : *values) if (value) worker.locality_domains.emplace_back(value->str());
    }
    if (const auto* values = descriptor->attachments()) {
        for (const auto* value : *values) {
            if (!value) invalid("MALFORMED_BUFFER");
            worker.attachments.push_back({value->family(),
                value->backend_id() ? value->backend_id()->str() : "",
                value->scheme() ? value->scheme()->str() : "",
                static_cast<LocalityKind>(value->locality()),
                value->domain() ? value->domain()->str() : ""});
        }
    }
    validate_worker(worker);
    return worker;
}

std::vector<std::byte> build(::labios::WorkerRegistryMessage::Kind kind, const WorkerInfo* worker,
                             int worker_id, uint64_t epoch,
                             std::span<const WorkerInfo> workers,
                             uint64_t generation, uint64_t captured_us) {
    flatbuffers::FlatBufferBuilder builder(4096);
    Payload payload_kind = Payload_NONE;
    flatbuffers::Offset<void> payload;
    PayloadKind envelope_kind = PayloadKind_Registration;
    switch (kind) {
    case ::labios::WorkerRegistryMessage::Kind::Registration: {
        if (!worker) invalid("INVALID_MESSAGE");
        payload = CreateWorkerRegistration(builder, make_worker(builder, *worker)).Union();
        payload_kind = Payload_WorkerRegistration;
        envelope_kind = PayloadKind_Registration;
        break;
    }
    case ::labios::WorkerRegistryMessage::Kind::ResourceUpdate: {
        if (!worker) invalid("INVALID_MESSAGE");
        validate_worker(*worker);
        payload = CreateWorkerResourceUpdate(
            builder, worker->id, worker->registration_epoch, worker->available,
            worker->total_capacity_bytes, worker->available_capacity_bytes,
            worker->capacity, worker->load, worker->skills, worker->compute,
            static_cast<uint8_t>(worker->reasoning)).Union();
        payload_kind = Payload_WorkerResourceUpdate;
        envelope_kind = PayloadKind_ResourceUpdate;
        break;
    }
    case ::labios::WorkerRegistryMessage::Kind::Deregistration:
        if (worker_id <= 0 || epoch == 0) invalid("INVALID_RANGE");
        payload = CreateWorkerDeregistration(builder, worker_id, epoch).Union();
        payload_kind = Payload_WorkerDeregistration;
        envelope_kind = PayloadKind_Deregistration;
        break;
    case ::labios::WorkerRegistryMessage::Kind::Snapshot: {
        std::set<int> ids;
        std::vector<flatbuffers::Offset<WorkerDescriptor>> rows;
        for (const auto& item : workers) {
            if (!ids.insert(item.id).second) invalid("DUPLICATE_WORKER_ID");
            rows.push_back(make_worker(builder, item));
        }
        payload = CreateWorkerRegistrySnapshot(
            builder, generation, captured_us, builder.CreateVector(rows)).Union();
        payload_kind = Payload_WorkerRegistrySnapshot;
        envelope_kind = PayloadKind_Snapshot;
        break;
    }
    }
    const auto message = CreateWorkerRegistryMessage(builder, 2, envelope_kind,
                                                       payload_kind, payload);
    builder.Finish(message, "LWR2");
    const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
    return {begin, begin + builder.GetSize()};
}
} // namespace wire

std::vector<std::byte> encode_worker_registration(const WorkerInfo& worker) {
    return wire::build(::labios::WorkerRegistryMessage::Kind::Registration, &worker, 0, 0, {}, 0, 0);
}

std::vector<std::byte> encode_worker_resource_update(const WorkerInfo& worker) {
    return wire::build(::labios::WorkerRegistryMessage::Kind::ResourceUpdate, &worker,
                       worker.id, worker.registration_epoch, {}, 0, 0);
}

std::vector<std::byte> encode_worker_deregistration(int worker_id, uint64_t registration_epoch) {
    return wire::build(::labios::WorkerRegistryMessage::Kind::Deregistration, nullptr,
                       worker_id, registration_epoch, {}, 0, 0);
}

std::vector<std::byte> encode_worker_snapshot(std::span<const WorkerInfo> workers,
                                              uint64_t generation, uint64_t captured_us) {
    return wire::build(::labios::WorkerRegistryMessage::Kind::Snapshot, nullptr, 0, 0,
                       workers, generation, captured_us);
}

WorkerInfo derive_worker_capabilities(
    WorkerInfo base, const BackendRegistry& backends,
    std::span<const std::string> pipeline_operations) {
    base.operations = {"core.read", "core.write", "core.composite"};
    base.operation_versions.assign(base.operations.size(), 1);
    base.pipeline_operations.clear();
    base.pipeline_operation_versions.clear();
    if (base.tier != WorkerTier::Databot) {
        base.pipeline_operations.assign(pipeline_operations.begin(), pipeline_operations.end());
        std::sort(base.pipeline_operations.begin(), base.pipeline_operations.end());
        base.pipeline_operations.erase(
            std::unique(base.pipeline_operations.begin(), base.pipeline_operations.end()),
            base.pipeline_operations.end());
        base.pipeline_operation_versions.assign(base.pipeline_operations.size(), 1);
    }
    base.attachments.clear();
    for (const auto& scheme : backends.schemes()) {
        std::optional<ResourceFamily> family;
        if (scheme == "file") family = ResourceFamily::FileRange;
        else if (scheme == "sqlite") family = ResourceFamily::Relational;
        else if (scheme == "kv") family = ResourceFamily::KeyValue;
        if (!family) continue;
        base.attachments.push_back({static_cast<uint8_t>(*family), "default", scheme,
                                    LocalityKind::Shared, {}});
    }
    return base;
}

WorkerRegistryMessage decode_worker_message(std::span<const std::byte> bytes) {
    if (bytes.empty()) throw std::runtime_error("MALFORMED_BUFFER");
    flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (!registry::VerifyWorkerRegistryMessageBuffer(verifier)) {
        throw std::runtime_error("MALFORMED_BUFFER");
    }
    const auto* message = registry::GetWorkerRegistryMessage(bytes.data());
    if (message->protocol_version() != 2) throw std::runtime_error("UNSUPPORTED_VERSION");
    const auto kind = message->kind();
    if (kind < registry::PayloadKind_Registration || kind > registry::PayloadKind_Snapshot) {
        throw std::runtime_error("INVALID_KIND");
    }
    const auto expected_payload = static_cast<registry::Payload>(
        static_cast<uint8_t>(kind) + 1U);
    if (message->payload_type() != expected_payload || message->payload() == nullptr) {
        throw std::runtime_error("WRONG_KIND");
    }

    WorkerRegistryMessage result;
    result.kind = static_cast<::labios::WorkerRegistryMessage::Kind>(kind);
    switch (result.kind) {
    case ::labios::WorkerRegistryMessage::Kind::Registration: {
        const auto* registration = message->payload_as_WorkerRegistration();
        if (!registration || !registration->worker()) throw std::runtime_error("MALFORMED_BUFFER");
        result.worker = wire::read_worker(registration->worker());
        break;
    }
    case ::labios::WorkerRegistryMessage::Kind::ResourceUpdate: {
        const auto* update = message->payload_as_WorkerResourceUpdate();
        if (!update || update->worker_id() <= 0 || update->registration_epoch() == 0 ||
            !std::isfinite(update->capacity()) || update->capacity() < 0.0 || update->capacity() > 1.0 ||
            !std::isfinite(update->load()) || update->load() < 0.0 || update->load() > 1.0 ||
            !std::isfinite(update->skills()) || update->skills() < 0.0 || update->skills() > 1.0 ||
            !std::isfinite(update->compute()) || update->compute() < 0.0 || update->compute() > 1.0) {
            throw std::runtime_error("INVALID_RANGE");
        }
        result.worker_id = update->worker_id();
        result.registration_epoch = update->registration_epoch();
        result.worker.id = result.worker_id;
        result.worker.registration_epoch = result.registration_epoch;
        result.worker.available = update->available();
        result.worker.total_capacity_bytes = update->total_capacity_bytes();
        result.worker.available_capacity_bytes = update->available_capacity_bytes();
        if (result.worker.total_capacity_bytes != 0 &&
            result.worker.available_capacity_bytes > result.worker.total_capacity_bytes) {
            throw std::runtime_error("INVALID_RANGE");
        }
        result.worker.capacity = update->capacity();
        result.worker.load = update->load();
        result.worker.skills = update->skills();
        result.worker.compute = update->compute();
        result.worker.reasoning = update->reasoning();
        break;
    }
    case ::labios::WorkerRegistryMessage::Kind::Deregistration: {
        const auto* deregistration = message->payload_as_WorkerDeregistration();
        if (!deregistration || deregistration->worker_id() <= 0 ||
            deregistration->registration_epoch() == 0) throw std::runtime_error("INVALID_RANGE");
        result.worker_id = deregistration->worker_id();
        result.registration_epoch = deregistration->registration_epoch();
        break;
    }
    case ::labios::WorkerRegistryMessage::Kind::Snapshot: {
        const auto* snapshot = message->payload_as_WorkerRegistrySnapshot();
        // Generation zero is the valid, verified empty initial snapshot.
        if (!snapshot || snapshot->captured_us() == 0) {
            throw std::runtime_error("INVALID_RANGE");
        }
        result.registry_generation = snapshot->registry_generation();
        result.captured_us = snapshot->captured_us();
        std::set<int> ids;
        if (const auto* rows = snapshot->workers()) {
            for (const auto* row : *rows) {
                auto worker = wire::read_worker(row);
                if (!ids.insert(worker.id).second) throw std::runtime_error("DUPLICATE_WORKER_ID");
                result.workers.push_back(std::move(worker));
            }
        }
        break;
    }
    }
    return result;
}

// These helpers are retained solely for old component tests and offline
// inspection. No runtime subject uses them; all runtime messages are v2.
std::string encode_worker_registry(std::span<const WorkerInfo> workers) {
    std::string result;
    for (const auto& worker : workers) {
        result += std::to_string(worker.id) + "," + (worker.available ? "1" : "0") + "," +
                  std::to_string(worker.capacity) + "," + std::to_string(worker.load) + "," +
                  std::to_string(worker.speed) + "," + std::to_string(worker.energy) + "," +
                  std::to_string(static_cast<int>(worker.tier)) + "\n";
    }
    return result;
}

WorkerRegistrySnapshot decode_worker_registry(std::string_view text) {
    WorkerRegistrySnapshot result;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string token;
        WorkerInfo worker;
        try {
            if (!std::getline(row, token, ',')) throw std::runtime_error("row");
            worker.id = std::stoi(token);
            if (!std::getline(row, token, ',')) throw std::runtime_error("row");
            worker.available = token == "1";
            if (!std::getline(row, token, ',')) throw std::runtime_error("row");
            worker.capacity = std::stod(token);
            if (!std::getline(row, token, ',')) throw std::runtime_error("row");
            worker.load = std::stod(token);
            if (!std::getline(row, token, ',')) throw std::runtime_error("row");
            worker.speed = std::stoi(token);
            if (!std::getline(row, token, ',')) throw std::runtime_error("row");
            worker.energy = std::stoi(token);
            if (std::getline(row, token, ',')) worker.tier = static_cast<WorkerTier>(std::stoi(token));
            result.workers.push_back(worker);
        } catch (...) {
            ++result.malformed_rows;
        }
    }
    return result;
}
} // namespace labios
