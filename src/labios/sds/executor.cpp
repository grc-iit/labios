#include <labios/sds/executor.h>

namespace labios::sds {

StageResult execute_pipeline(const Pipeline& pipeline,
                              std::span<const std::byte> input_data,
                              const ProgramRepository& repo) {
    if (pipeline.empty())
        return {true, {}, {input_data.begin(), input_data.end()}};

    std::vector<std::byte> current(input_data.begin(), input_data.end());

    for (size_t i = 0; i < pipeline.stages.size(); ++i) {
        auto& stage = pipeline.stages[i];

        auto* fn = repo.lookup(stage.operation);
        if (!fn)
            return {false, "unknown SDS function: " + stage.operation, {}};

        auto result = (*fn)(std::span<const std::byte>(current), stage.args);
        if (!result.success)
            return result;

        current = std::move(result.data);
    }

    return {true, {}, std::move(current)};
}

} // namespace labios::sds
