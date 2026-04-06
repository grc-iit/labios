#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/sds/executor.h>
#include <labios/sds/program_repo.h>
#include <labios/sds/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <vector>

namespace {

std::vector<std::byte> make_uint64_data(size_t total_bytes) {
    size_t count = total_bytes / sizeof(uint64_t);
    std::vector<std::byte> buf(count * sizeof(uint64_t));
    for (size_t i = 0; i < count; ++i) {
        uint64_t val = count - i; // descending so sort has work
        std::memcpy(buf.data() + i * sizeof(uint64_t), &val, sizeof(uint64_t));
    }
    return buf;
}

labios::sds::Pipeline rag_pipeline() {
    labios::sds::Pipeline p;
    // Simulate: read (identity) -> chunk (truncate to half) -> embed (sort as proxy)
    p.stages.push_back({"builtin://identity", "", -1, -1});
    p.stages.push_back({"builtin://truncate", "", -1, -1}); // args set per-call
    p.stages.push_back({"builtin://sort_uint64", "", -1, -1});
    return p;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: RAG pipeline produces sorted, truncated output
// ---------------------------------------------------------------------------

TEST_CASE("RAG pipeline: 3-stage produces sorted truncated output", "[bench][rag]") {
    labios::sds::ProgramRepository repo;
    auto pipeline = rag_pipeline();
    // Truncate to 5 uint64s
    pipeline.stages[1].args = std::to_string(5 * sizeof(uint64_t));

    auto input = make_uint64_data(1024); // ~128 uint64s
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);

    REQUIRE(result.success);
    REQUIRE(result.data.size() == 5 * sizeof(uint64_t));

    // Verify sorted ascending
    for (size_t i = 1; i < 5; ++i) {
        uint64_t prev, curr;
        std::memcpy(&prev, result.data.data() + (i - 1) * sizeof(uint64_t), sizeof(uint64_t));
        std::memcpy(&curr, result.data.data() + i * sizeof(uint64_t), sizeof(uint64_t));
        REQUIRE(prev <= curr);
    }
}

TEST_CASE("RAG pipeline: empty input handled gracefully", "[bench][rag]") {
    labios::sds::ProgramRepository repo;
    auto pipeline = rag_pipeline();
    pipeline.stages[1].args = "0";

    std::vector<std::byte> empty;
    auto result = labios::sds::execute_pipeline(pipeline, empty, repo);
    REQUIRE(result.success);
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("RAG pipeline benchmarks", "[bench][rag][!benchmark]") {
    labios::sds::ProgramRepository repo;

    auto input_10k = make_uint64_data(10 * 1024);
    auto input_100k = make_uint64_data(100 * 1024);
    auto input_1m = make_uint64_data(1024 * 1024);

    auto pipeline_10k = rag_pipeline();
    pipeline_10k.stages[1].args = std::to_string(input_10k.size() / 2);

    auto pipeline_100k = rag_pipeline();
    pipeline_100k.stages[1].args = std::to_string(input_100k.size() / 2);

    auto pipeline_1m = rag_pipeline();
    pipeline_1m.stages[1].args = std::to_string(input_1m.size() / 2);

    BENCHMARK("Pipeline 3-stage 10KB") {
        return labios::sds::execute_pipeline(pipeline_10k, input_10k, repo);
    };

    BENCHMARK("Pipeline 3-stage 100KB") {
        return labios::sds::execute_pipeline(pipeline_100k, input_100k, repo);
    };

    BENCHMARK("Pipeline 3-stage 1MB") {
        return labios::sds::execute_pipeline(pipeline_1m, input_1m, repo);
    };

    BENCHMARK("Pipeline setup (serialize + deserialize)") {
        auto s = labios::sds::serialize_pipeline(pipeline_10k);
        return labios::sds::deserialize_pipeline(s);
    };
}
