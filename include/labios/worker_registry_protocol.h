#pragma once
#include <labios/solver/solver.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace labios {
class BackendRegistry;
struct WorkerRegistrySnapshot { uint64_t registry_generation=0; uint64_t captured_us=0; std::vector<WorkerInfo> workers; size_t malformed_rows=0; };
struct WorkerRegistryMessage { enum class Kind { Registration, ResourceUpdate, Deregistration, Snapshot }; Kind kind=Kind::Snapshot; WorkerInfo worker{}; int worker_id=0; uint64_t registration_epoch=0; std::vector<WorkerInfo> workers; uint64_t registry_generation=0; uint64_t captured_us=0; };
std::string encode_worker_registry(std::span<const WorkerInfo> workers);
WorkerRegistrySnapshot decode_worker_registry(std::string_view payload);
std::vector<std::byte> encode_worker_registration(const WorkerInfo&);
std::vector<std::byte> encode_worker_resource_update(const WorkerInfo&);
std::vector<std::byte> encode_worker_deregistration(int worker_id,uint64_t registration_epoch);
std::vector<std::byte> encode_worker_snapshot(std::span<const WorkerInfo>,uint64_t generation,uint64_t captured_us);
WorkerRegistryMessage decode_worker_message(std::span<const std::byte>);
/// Derive executable capabilities from the worker's constructed external
/// backends, tier, and loaded pipeline repository.
WorkerInfo derive_worker_capabilities(WorkerInfo base, const BackendRegistry& backends,
                                      std::span<const std::string> pipeline_operations);
}