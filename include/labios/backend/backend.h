#pragma once
#include <labios/label.h>
#include <cstddef>
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

struct BackendQueryResult {
    bool success = true;
    std::string error;
    std::string json_data;
};

/// Concept for storage backends (LABIOS-SPEC Section 4.4).
/// Every backend receives the full label. The label carries intent, isolation,
/// priority, URIs, and pipeline context. Backends use this metadata to make
/// intelligent storage decisions.
template<typename B>
concept BackendStore = requires(B b, const LabelData& label,
    std::span<const std::byte> data) {
    { b.put(label, data) } -> std::same_as<BackendResult>;
    { b.get(label) }       -> std::same_as<BackendDataResult>;
    { b.del(label) }       -> std::same_as<BackendResult>;
    { b.query(label) }     -> std::same_as<BackendQueryResult>;
    { b.scheme() }         -> std::same_as<std::string_view>;
};

} // namespace labios
