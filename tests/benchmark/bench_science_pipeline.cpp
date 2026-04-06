#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/sds/executor.h>
#include <labios/sds/program_repo.h>
#include <labios/sds/types.h>

#include <cstddef>
#include <numeric>
#include <vector>

namespace {

std::vector<std::byte> make_compressible_data(size_t size) {
    std::vector<std::byte> data(size);
    // Blocks of 64 identical bytes give RLE significant compression
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<std::byte>((i / 64) % 256);
    }
    return data;
}

labios::sds::Pipeline science_pipeline() {
    labios::sds::Pipeline p;
    // transform (compress_rle) -> write staging (truncate to 90%)
    p.stages.push_back({"builtin://compress_rle", "", -1, -1});
    p.stages.push_back({"builtin://truncate", "", -1, -1}); // args set per-call
    return p;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: science pipeline compresses and truncates
// ---------------------------------------------------------------------------

TEST_CASE("Science pipeline: compress then truncate", "[bench][science]") {
    labios::sds::ProgramRepository repo;
    auto input = make_compressible_data(4096);

    auto pipeline = science_pipeline();
    // After compression, truncate to 64 bytes (or whatever is available)
    pipeline.stages[1].args = "64";

    auto result = labios::sds::execute_pipeline(pipeline, input, repo);
    REQUIRE(result.success);
    REQUIRE(result.data.size() <= 64);
}

TEST_CASE("Science pipeline: large input compresses", "[bench][science]") {
    labios::sds::ProgramRepository repo;
    auto input = make_compressible_data(1024 * 1024);

    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://compress_rle", "", -1, -1});

    auto result = labios::sds::execute_pipeline(pipeline, input, repo);
    REQUIRE(result.success);
    REQUIRE(result.data.size() < input.size());
}

// ---------------------------------------------------------------------------
// Benchmarks: measure throughput
// ---------------------------------------------------------------------------

TEST_CASE("Science pipeline benchmarks", "[bench][science][!benchmark]") {
    labios::sds::ProgramRepository repo;

    auto input_1m = make_compressible_data(1024 * 1024);
    auto input_10m = make_compressible_data(10 * 1024 * 1024);

    auto pipeline_1m = science_pipeline();
    pipeline_1m.stages[1].args = std::to_string(input_1m.size());

    auto pipeline_10m = science_pipeline();
    pipeline_10m.stages[1].args = std::to_string(input_10m.size());

    BENCHMARK("Pipeline throughput 1MB") {
        return labios::sds::execute_pipeline(pipeline_1m, input_1m, repo);
    };

    BENCHMARK("Pipeline throughput 10MB") {
        return labios::sds::execute_pipeline(pipeline_10m, input_10m, repo);
    };

    BENCHMARK("Compress-only 1MB") {
        labios::sds::Pipeline p;
        p.stages.push_back({"builtin://compress_rle", "", -1, -1});
        return labios::sds::execute_pipeline(p, input_1m, repo);
    };

    BENCHMARK("Compress-only 10MB") {
        labios::sds::Pipeline p;
        p.stages.push_back({"builtin://compress_rle", "", -1, -1});
        return labios::sds::execute_pipeline(p, input_10m, repo);
    };
}
