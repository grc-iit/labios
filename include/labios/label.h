#pragma once

#include <labios/sds/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace labios {

enum class LabelType : uint8_t { Read, Write, Delete, Flush, Composite, Observe };
enum class Intent : uint8_t {
    None, Checkpoint, Cache, ToolOutput, FinalResult,
    Intermediate, SharedState,
    Embedding, ModelWeight, KVCache, ReasoningTrace
};
enum class Isolation : uint8_t { None, Agent, Workspace, Global };
enum class HazardType : uint8_t { RAW, WAW, WAR };
enum class CompletionStatus : uint8_t { Complete, Error };
enum class Durability : uint8_t { Ephemeral, Durable };
enum class StatusCode : uint8_t { Created, Queued, Shuffled, Scheduled, Executing, Complete, Failed };
enum class ContinuationKind : uint8_t { None, Notify, Chain, Conditional };

namespace LabelFlags {
    constexpr uint32_t Queued      = 1 << 0;
    constexpr uint32_t Scheduled   = 1 << 1;
    constexpr uint32_t Pending     = 1 << 2;
    constexpr uint32_t Cached      = 1 << 3;
    constexpr uint32_t Invalidated = 1 << 4;
    constexpr uint32_t Async       = 1 << 5;
    constexpr uint32_t HighPrio    = 1 << 6;
} // namespace LabelFlags

struct MemoryPtr {
    uint64_t address;
    uint64_t size;
};

struct FilePath {
    std::string path;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct NetworkEndpoint {
    std::string host;
    uint16_t port;
};

using Pointer = std::variant<std::monostate, MemoryPtr, FilePath, NetworkEndpoint>;

Pointer memory_ptr(const void* addr, uint64_t size);
Pointer file_path(std::string_view path);
Pointer file_path(std::string_view path, uint64_t offset, uint64_t length);
Pointer network_endpoint(std::string_view host, uint16_t port);

struct LabelDependency {
    uint64_t label_id = 0;
    HazardType hazard_type = HazardType::RAW;
};

struct Continuation {
    ContinuationKind kind = ContinuationKind::None;
    std::string target_channel;
    std::string chain_params;
    std::string condition;
};

struct RoutingDecision {
    uint32_t worker_id = 0;
    std::string policy;
};

struct HopRecord {
    std::string component;
    uint64_t timestamp_us = 0;
};

struct LabelData {
    uint64_t id = 0;
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    uint32_t app_id = 0;
    std::vector<LabelDependency> dependencies;
    uint64_t data_size = 0;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;
    std::string reply_to;
    std::string file_key;              // Normalized path for shuffler grouping
    std::vector<uint64_t> children;    // Supertask child label IDs

    // Spec fields
    uint64_t version = 0;
    Durability durability = Durability::Ephemeral;
    Continuation continuation;
    std::string source_uri;
    std::string dest_uri;
    sds::Pipeline pipeline;

    // Accumulation (written by runtime)
    RoutingDecision routing;
    std::vector<HopRecord> hops;

    // State
    StatusCode status = StatusCode::Created;
    uint64_t created_us = 0;
    uint64_t completed_us = 0;
};

struct LabelParams {
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    std::vector<LabelDependency> dependencies;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;

    // Spec additions (Wave 10)
    uint64_t version = 0;
    Durability durability = Durability::Ephemeral;
    Continuation continuation;
    std::string source_uri;
    std::string dest_uri;
    sds::Pipeline pipeline;
};

struct CompletionData {
    uint64_t label_id = 0;
    CompletionStatus status = CompletionStatus::Complete;
    std::string error;
    std::string data_key;
};

uint64_t generate_label_id(uint32_t app_id);

std::vector<std::byte> serialize_label(const LabelData& label);
LabelData deserialize_label(std::span<const std::byte> buf);

std::vector<std::byte> serialize_completion(const CompletionData& completion);
CompletionData deserialize_completion(std::span<const std::byte> buf);

} // namespace labios
