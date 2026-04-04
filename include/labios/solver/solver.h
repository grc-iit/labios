#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace labios {

struct WorkerInfo {
    int id;
    bool available = true;
};

using AssignmentMap = std::unordered_map<int, std::vector<std::vector<std::byte>>>;

template<typename T>
concept Solver = requires(T s,
    std::vector<std::vector<std::byte>> labels,
    std::vector<WorkerInfo> workers) {
    { s.assign(std::move(labels), std::move(workers)) } -> std::same_as<AssignmentMap>;
};

} // namespace labios
