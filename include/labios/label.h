#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace labios {

enum class LabelType : uint8_t { Read, Write, Delete, Flush, Composite };
enum class Intent : uint8_t {
    None, Checkpoint, Cache, ToolOutput, FinalResult, Intermediate, SharedState
};
enum class Isolation : uint8_t { None, Application, Agent };
enum class HazardType : uint8_t { RAW, WAW, WAR };
enum class CompletionStatus : uint8_t { Complete, Error };

namespace LabelFlags {
    constexpr uint32_t Queued      = 1 << 0;
    constexpr uint32_t Scheduled   = 1 << 1;
    constexpr uint32_t Pending     = 1 << 2;
    constexpr uint32_t Cached      = 1 << 3;
    constexpr uint32_t Invalidated = 1 << 4;
    constexpr uint32_t Async       = 1 << 5;
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
};

struct LabelParams {
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;
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
