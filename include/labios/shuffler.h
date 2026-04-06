#pragma once

#include <labios/label.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace labios {

struct ShufflerConfig {
    bool aggregation_enabled = true;
    std::string dep_granularity = "per-file";
};

struct Supertask {
    LabelData composite;
    std::vector<LabelData> children;
};

struct ShuffleResult {
    std::vector<std::pair<LabelData, int>> direct_route;
    std::vector<Supertask> supertasks;
    std::vector<LabelData> independent;

    // For aggregated labels: merged_label_id -> all original reply_to addresses.
    std::unordered_map<uint64_t, std::vector<std::string>> reply_fanout;
};

class Shuffler {
public:
    explicit Shuffler(ShufflerConfig config);

    using LocationLookup = std::function<std::optional<int>(
        const std::string& file_key, uint64_t offset, uint64_t length)>;

    ShuffleResult shuffle(std::vector<LabelData> batch, LocationLookup lookup);

private:
    ShufflerConfig config_;

    std::vector<LabelData> aggregate(
        std::vector<LabelData>& labels,
        std::unordered_map<uint64_t, std::vector<std::string>>& reply_fanout);
    void detect_dependencies(std::vector<LabelData>& labels);
    std::vector<Supertask> build_supertasks(std::vector<LabelData>& labels);
};

/// Pack multiple serialized labels into one buffer.
/// Format: [uint32 count][uint32 len_0][bytes_0][uint32 len_1][bytes_1]...
std::vector<std::byte> pack_labels(
    const std::vector<std::vector<std::byte>>& labels);

/// Unpack a buffer produced by pack_labels.
std::vector<std::vector<std::byte>> unpack_labels(
    std::span<const std::byte> packed);

} // namespace labios
