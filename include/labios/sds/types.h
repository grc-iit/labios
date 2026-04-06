#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios::sds {

/// Result of a pipeline stage execution.
struct StageResult {
    bool success = true;
    std::string error;
    std::vector<std::byte> data;
};

/// Function signature that all SDS operations implement.
using SdsFunction = std::function<StageResult(std::span<const std::byte> input,
                                               std::string_view args)>;

/// A stage in a pipeline.
struct PipelineStage {
    std::string operation;   // "builtin://compress_rle" or "repo://my_func"
    std::string args;        // Serialized arguments
    int input_stage = -1;    // -1 = use label source data, >= 0 = output of stage[N]
    int output_stage = -1;   // -1 = write to label destination, >= 0 = input to stage[N]
};

/// A complete pipeline definition.
struct Pipeline {
    std::vector<PipelineStage> stages;
    bool empty() const { return stages.empty(); }
};

/// Serialize a pipeline to a string (for FlatBuffers storage).
std::string serialize_pipeline(const Pipeline& p);

/// Deserialize a pipeline from a string.
Pipeline deserialize_pipeline(std::string_view s);

} // namespace labios::sds
