#pragma once

#include <labios/solver/solver.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

/// Decoded response from the manager's worker registry endpoint.
///
/// Wire format is line-oriented for backwards compatibility:
///   id,available,capacity,load,speed,energy,tier
///
/// Rows without a tier are accepted as legacy Databot workers.
struct WorkerRegistrySnapshot {
    std::vector<WorkerInfo> workers;
    size_t malformed_rows = 0;
};

std::string encode_worker_registry(std::span<const WorkerInfo> workers);
WorkerRegistrySnapshot decode_worker_registry(std::string_view payload);

} // namespace labios
