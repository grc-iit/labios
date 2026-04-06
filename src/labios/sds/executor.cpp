#include <labios/sds/executor.h>

namespace labios::sds {

StageResult execute_pipeline(const Pipeline& pipeline,
                              std::span<const std::byte> input_data,
                              const ProgramRepository& repo) {
    if (pipeline.empty())
        return {true, {}, {input_data.begin(), input_data.end()}};

    // Stage outputs indexed by stage number.
    std::vector<std::vector<std::byte>> stage_outputs(pipeline.stages.size());
    std::vector<std::byte> source_data(input_data.begin(), input_data.end());

    for (size_t i = 0; i < pipeline.stages.size(); ++i) {
        auto& stage = pipeline.stages[i];

        auto* fn = repo.lookup(stage.operation);
        if (!fn)
            return {false, "unknown SDS function: " + stage.operation, {}};

        // Determine input: -1 means label source data, >= 0 means output of stage[N]
        std::span<const std::byte> stage_input;
        if (stage.input_stage < 0) {
            stage_input = std::span<const std::byte>(source_data);
        } else if (stage.input_stage < static_cast<int>(i)) {
            stage_input = std::span<const std::byte>(stage_outputs[stage.input_stage]);
        } else {
            return {false, "stage " + std::to_string(i)
                    + " references future or self input_stage="
                    + std::to_string(stage.input_stage), {}};
        }

        auto result = (*fn)(stage_input, stage.args);
        if (!result.success)
            return result;

        stage_outputs[i] = std::move(result.data);
    }

    // Final output: last stage with output_stage == -1, or last stage
    for (int i = static_cast<int>(pipeline.stages.size()) - 1; i >= 0; --i) {
        if (pipeline.stages[i].output_stage < 0) {
            return {true, {}, std::move(stage_outputs[i])};
        }
    }
    // Fallback: return last stage output
    return {true, {}, std::move(stage_outputs.back())};
}

} // namespace labios::sds
