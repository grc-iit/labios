#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

struct BackendResult {
    bool success = true;
    std::string error;
};

struct BackendDataResult {
    bool success = true;
    std::string error;
    std::vector<std::byte> data;
};

/// Concept for storage backends. Each backend handles one or more URI schemes.
template<typename B>
concept BackendStore = requires(B b,
    std::string_view path, uint64_t offset, uint64_t length,
    std::span<const std::byte> data) {
    { b.put(path, offset, data) } -> std::same_as<BackendResult>;
    { b.get(path, offset, length) } -> std::same_as<BackendDataResult>;
    { b.del(path) } -> std::same_as<BackendResult>;
    { b.scheme() } -> std::same_as<std::string_view>;
};

} // namespace labios
