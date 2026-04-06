#pragma once
#include <labios/sds/program_repo.h>
#include <labios/sds/types.h>

#include <span>

namespace labios::sds {

/// Execute a pipeline against input data.
/// Stages run in order. Each stage's output feeds the next stage's input.
/// Returns the final stage's output or the first error encountered.
StageResult execute_pipeline(const Pipeline& pipeline,
                              std::span<const std::byte> input_data,
                              const ProgramRepository& repo);

} // namespace labios::sds
