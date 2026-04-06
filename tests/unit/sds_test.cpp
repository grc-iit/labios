#include <catch2/catch_test_macros.hpp>
#include <labios/label.h>
#include <labios/sds/executor.h>
#include <labios/sds/program_repo.h>
#include <labios/sds/types.h>
#include <labios/solver/solver.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <vector>

namespace {

std::vector<std::byte> make_uint64_array(std::initializer_list<uint64_t> vals) {
    std::vector<std::byte> out(vals.size() * sizeof(uint64_t));
    size_t i = 0;
    for (auto v : vals) {
        std::memcpy(out.data() + i * sizeof(uint64_t), &v, sizeof(uint64_t));
        ++i;
    }
    return out;
}

uint64_t read_uint64(std::span<const std::byte> data, size_t index = 0) {
    uint64_t v;
    std::memcpy(&v, data.data() + index * sizeof(uint64_t), sizeof(uint64_t));
    return v;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ProgramRepository
// ---------------------------------------------------------------------------

TEST_CASE("ProgramRepository registers builtins", "[sds]") {
    labios::sds::ProgramRepository repo;

    REQUIRE(repo.has("builtin://identity"));
    REQUIRE(repo.has("builtin://compress_rle"));
    REQUIRE(repo.has("builtin://decompress_rle"));
    REQUIRE(repo.has("builtin://filter_bytes"));
    REQUIRE(repo.has("builtin://sum_uint64"));
    REQUIRE(repo.has("builtin://sort_uint64"));
    REQUIRE(repo.has("builtin://sample"));
    REQUIRE(repo.has("builtin://truncate"));

    REQUIRE(repo.has("builtin://deduplicate"));
    REQUIRE(repo.has("builtin://median_uint64"));
    REQUIRE(repo.has("builtin://format_convert"));

    auto names = repo.list();
    REQUIRE(names.size() == 11);
}

TEST_CASE("ProgramRepository lookup returns nullptr for unknown", "[sds]") {
    labios::sds::ProgramRepository repo;
    REQUIRE(repo.lookup("nonexistent") == nullptr);
    REQUIRE_FALSE(repo.has("nonexistent"));
}

TEST_CASE("Custom function registration and execution", "[sds]") {
    labios::sds::ProgramRepository repo;

    repo.register_function("repo://double_size",
        [](std::span<const std::byte> input, std::string_view /*args*/) -> labios::sds::StageResult {
            std::vector<std::byte> out;
            out.insert(out.end(), input.begin(), input.end());
            out.insert(out.end(), input.begin(), input.end());
            return {true, {}, std::move(out)};
        });

    REQUIRE(repo.has("repo://double_size"));

    std::vector<std::byte> input = {std::byte{0x01}, std::byte{0x02}};
    auto* fn = repo.lookup("repo://double_size");
    REQUIRE(fn != nullptr);

    auto result = (*fn)(input, "");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 4);
}

// ---------------------------------------------------------------------------
// Builtin: identity
// ---------------------------------------------------------------------------

TEST_CASE("builtin://identity pass-through", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://identity");
    REQUIRE(fn != nullptr);

    std::vector<std::byte> input = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    auto result = (*fn)(input, "");
    REQUIRE(result.success);
    REQUIRE(result.data == input);
}

// ---------------------------------------------------------------------------
// Builtin: sort_uint64
// ---------------------------------------------------------------------------

TEST_CASE("builtin://sort_uint64 sorts ascending", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://sort_uint64");

    auto input = make_uint64_array({50, 10, 40, 20, 30});
    auto result = (*fn)(input, "");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 5 * sizeof(uint64_t));

    REQUIRE(read_uint64(result.data, 0) == 10);
    REQUIRE(read_uint64(result.data, 1) == 20);
    REQUIRE(read_uint64(result.data, 2) == 30);
    REQUIRE(read_uint64(result.data, 3) == 40);
    REQUIRE(read_uint64(result.data, 4) == 50);
}

// ---------------------------------------------------------------------------
// Builtin: sum_uint64
// ---------------------------------------------------------------------------

TEST_CASE("builtin://sum_uint64 computes correct sum", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://sum_uint64");

    auto input = make_uint64_array({10, 20, 30, 40});
    auto result = (*fn)(input, "");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == sizeof(uint64_t));
    REQUIRE(read_uint64(result.data) == 100);
}

TEST_CASE("builtin://sum_uint64 empty input returns zero", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://sum_uint64");

    std::vector<std::byte> empty;
    auto result = (*fn)(empty, "");
    REQUIRE(result.success);
    REQUIRE(read_uint64(result.data) == 0);
}

// ---------------------------------------------------------------------------
// Builtin: truncate
// ---------------------------------------------------------------------------

TEST_CASE("builtin://truncate returns first N bytes", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://truncate");

    std::vector<std::byte> input = {
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};

    auto result = (*fn)(input, "3");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 3);
    REQUIRE(result.data[0] == std::byte{1});
    REQUIRE(result.data[2] == std::byte{3});
}

TEST_CASE("builtin://truncate with N > input returns full input", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://truncate");

    std::vector<std::byte> input = {std::byte{1}, std::byte{2}};
    auto result = (*fn)(input, "100");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 2);
}

// ---------------------------------------------------------------------------
// Builtin: sample
// ---------------------------------------------------------------------------

TEST_CASE("builtin://sample returns N bytes", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://sample");

    std::vector<std::byte> input(100);
    std::iota(reinterpret_cast<uint8_t*>(input.data()),
              reinterpret_cast<uint8_t*>(input.data()) + input.size(), 0);

    auto result = (*fn)(input, "10");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 10);
}

TEST_CASE("builtin://sample with N >= input returns full input", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://sample");

    std::vector<std::byte> input = {std::byte{0xAA}, std::byte{0xBB}};
    auto result = (*fn)(input, "5");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 2);
}

// ---------------------------------------------------------------------------
// Builtin: compress/decompress RLE roundtrip
// ---------------------------------------------------------------------------

TEST_CASE("compress_rle/decompress_rle roundtrip", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* compress = repo.lookup("builtin://compress_rle");
    auto* decompress = repo.lookup("builtin://decompress_rle");

    // Data with runs: 5 x 0xAA, 3 x 0xBB, 1 x 0xCC
    std::vector<std::byte> input;
    input.insert(input.end(), 5, std::byte{0xAA});
    input.insert(input.end(), 3, std::byte{0xBB});
    input.push_back(std::byte{0xCC});

    auto compressed = (*compress)(input, "");
    REQUIRE(compressed.success);
    REQUIRE(compressed.data.size() < input.size()); // RLE should compress runs

    auto decompressed = (*decompress)(compressed.data, "");
    REQUIRE(decompressed.success);
    REQUIRE(decompressed.data == input);
}

// ---------------------------------------------------------------------------
// Pipeline execution
// ---------------------------------------------------------------------------

TEST_CASE("Pipeline with 2 stages: sort then truncate", "[sds]") {
    labios::sds::ProgramRepository repo;

    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://sort_uint64", "", -1, -1});
    pipeline.stages.push_back({"builtin://truncate",
        std::to_string(2 * sizeof(uint64_t)), 0, -1});

    auto input = make_uint64_array({50, 10, 40, 20, 30});
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);

    REQUIRE(result.success);
    REQUIRE(result.data.size() == 2 * sizeof(uint64_t));
    REQUIRE(read_uint64(result.data, 0) == 10);
    REQUIRE(read_uint64(result.data, 1) == 20);
}

TEST_CASE("Pipeline with unknown function returns error", "[sds]") {
    labios::sds::ProgramRepository repo;

    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://nonexistent", "", -1, -1});

    std::vector<std::byte> input = {std::byte{1}};
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);

    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("unknown SDS function") != std::string::npos);
}

TEST_CASE("Pipeline with empty stages is a no-op", "[sds]") {
    labios::sds::ProgramRepository repo;
    labios::sds::Pipeline pipeline; // empty

    std::vector<std::byte> input = {std::byte{0xAA}, std::byte{0xBB}};
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);

    REQUIRE(result.success);
    REQUIRE(result.data == input);
}

// ---------------------------------------------------------------------------
// Pipeline serialization roundtrip
// ---------------------------------------------------------------------------

TEST_CASE("Pipeline serialization/deserialization roundtrip", "[sds]") {
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://sort_uint64", "", -1, -1});
    pipeline.stages.push_back({"builtin://truncate", "16", 0, -1});
    pipeline.stages.push_back({"repo://custom", "arg1", 1, -1});

    auto serialized = labios::sds::serialize_pipeline(pipeline);
    REQUIRE_FALSE(serialized.empty());

    auto deserialized = labios::sds::deserialize_pipeline(serialized);
    REQUIRE(deserialized.stages.size() == 3);

    REQUIRE(deserialized.stages[0].operation == "builtin://sort_uint64");
    REQUIRE(deserialized.stages[0].args.empty());
    REQUIRE(deserialized.stages[0].input_stage == -1);
    REQUIRE(deserialized.stages[0].output_stage == -1);

    REQUIRE(deserialized.stages[1].operation == "builtin://truncate");
    REQUIRE(deserialized.stages[1].args == "16");
    REQUIRE(deserialized.stages[1].input_stage == 0);
    REQUIRE(deserialized.stages[1].output_stage == -1);

    REQUIRE(deserialized.stages[2].operation == "repo://custom");
    REQUIRE(deserialized.stages[2].args == "arg1");
}

TEST_CASE("Empty pipeline serialization roundtrip", "[sds]") {
    labios::sds::Pipeline pipeline;
    auto serialized = labios::sds::serialize_pipeline(pipeline);
    REQUIRE(serialized.empty());

    auto deserialized = labios::sds::deserialize_pipeline(serialized);
    REQUIRE(deserialized.empty());
}

// ---------------------------------------------------------------------------
// Pipeline in Label serialization roundtrip
// ---------------------------------------------------------------------------

TEST_CASE("Label with pipeline roundtrips through FlatBuffers", "[sds]") {
    labios::LabelData label;
    label.id = 999;
    label.type = labios::LabelType::Write;
    label.source = labios::file_path("/input");
    label.destination = labios::file_path("/output");
    label.pipeline.stages.push_back({"builtin://compress_rle", "", -1, -1});
    label.pipeline.stages.push_back({"builtin://identity", "", -1, -1});

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.id == 999);
    REQUIRE_FALSE(result.pipeline.empty());
    REQUIRE(result.pipeline.stages.size() == 2);
    REQUIRE(result.pipeline.stages[0].operation == "builtin://compress_rle");
    REQUIRE(result.pipeline.stages[1].operation == "builtin://identity");
}

TEST_CASE("Label without pipeline roundtrips with empty pipeline", "[sds]") {
    labios::LabelData label;
    label.id = 1000;
    label.type = labios::LabelType::Read;

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.pipeline.empty());
}

// ---------------------------------------------------------------------------
// Tier gating: Databot rejects pipelines
// ---------------------------------------------------------------------------

TEST_CASE("Databot tier is 0, Pipeline tier is 1, Agentic tier is 2", "[sds]") {
    REQUIRE(static_cast<int>(labios::WorkerTier::Databot) == 0);
    REQUIRE(static_cast<int>(labios::WorkerTier::Pipeline) == 1);
    REQUIRE(static_cast<int>(labios::WorkerTier::Agentic) == 2);
}

// ---------------------------------------------------------------------------
// Builtin: deduplicate
// ---------------------------------------------------------------------------

TEST_CASE("SDS deduplicate removes consecutive duplicates", "[sds]") {
    labios::sds::ProgramRepository repo;

    std::vector<std::byte> input = {
        std::byte{1}, std::byte{1}, std::byte{2}, std::byte{2}, std::byte{3}, std::byte{1}
    };
    auto* fn = repo.lookup("builtin://deduplicate");
    REQUIRE(fn != nullptr);
    auto result = (*fn)(std::span<const std::byte>(input), "");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 4); // 1, 2, 3, 1
}

TEST_CASE("SDS deduplicate on empty input", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://deduplicate");
    std::vector<std::byte> empty;
    auto result = (*fn)(std::span<const std::byte>(empty), "");
    REQUIRE(result.success);
    REQUIRE(result.data.empty());
}

// ---------------------------------------------------------------------------
// Builtin: median_uint64
// ---------------------------------------------------------------------------

TEST_CASE("SDS median_uint64 odd count", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://median_uint64");
    REQUIRE(fn != nullptr);

    auto input = make_uint64_array({50, 10, 30, 20, 40});
    auto result = (*fn)(std::span<const std::byte>(input), "");
    REQUIRE(result.success);
    REQUIRE(read_uint64(result.data) == 30);
}

TEST_CASE("SDS median_uint64 even count", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://median_uint64");

    auto input = make_uint64_array({10, 20, 30, 40});
    auto result = (*fn)(std::span<const std::byte>(input), "");
    REQUIRE(result.success);
    REQUIRE(read_uint64(result.data) == 25);
}

TEST_CASE("SDS median_uint64 rejects empty", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://median_uint64");

    std::vector<std::byte> empty;
    auto result = (*fn)(std::span<const std::byte>(empty), "");
    REQUIRE_FALSE(result.success);
}

// ---------------------------------------------------------------------------
// Builtin: format_convert
// ---------------------------------------------------------------------------

TEST_CASE("SDS format_convert upper", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://format_convert");
    REQUIRE(fn != nullptr);

    std::vector<std::byte> input = {std::byte{'h'}, std::byte{'i'}};
    auto result = (*fn)(std::span<const std::byte>(input), "upper");
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 2);
    REQUIRE(static_cast<char>(result.data[0]) == 'H');
    REQUIRE(static_cast<char>(result.data[1]) == 'I');
}

TEST_CASE("SDS format_convert unknown is identity", "[sds]") {
    labios::sds::ProgramRepository repo;
    auto* fn = repo.lookup("builtin://format_convert");

    std::vector<std::byte> input = {std::byte{0xAA}, std::byte{0xBB}};
    auto result = (*fn)(std::span<const std::byte>(input), "parquet");
    REQUIRE(result.success);
    REQUIRE(result.data == input);
}

// ---------------------------------------------------------------------------
// DAG-aware executor
// ---------------------------------------------------------------------------

TEST_CASE("SDS executor respects input_stage field", "[sds]") {
    labios::sds::ProgramRepository repo;

    // Stage 0: truncate to 2 bytes. Stage 1: identity on SOURCE data (not stage 0).
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://truncate", "2", -1, -1});
    pipeline.stages.push_back({"builtin://identity", "", -1, -1});

    std::vector<std::byte> input = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);
    REQUIRE(result.success);
    // Stage 1 reads from source (-1), so output is full 4 bytes
    REQUIRE(result.data.size() == 4);
}

TEST_CASE("SDS executor chains stage outputs via input_stage", "[sds]") {
    labios::sds::ProgramRepository repo;

    // Stage 0: identity (pass-through). Stage 1: truncate reading from stage 0.
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://identity", "", -1, -1});
    pipeline.stages.push_back({"builtin://truncate", "2", 0, -1});

    std::vector<std::byte> input = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 2);
}

TEST_CASE("SDS executor rejects forward input_stage reference", "[sds]") {
    labios::sds::ProgramRepository repo;

    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://identity", "", 1, -1}); // references stage 1 (forward)

    std::vector<std::byte> input = {std::byte{1}};
    auto result = labios::sds::execute_pipeline(pipeline, input, repo);
    REQUIRE_FALSE(result.success);
    REQUIRE(result.error.find("future or self") != std::string::npos);
}
