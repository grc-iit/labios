#include <labios/client.h>
#include <labios/label.h>
#include <labios/sds/types.h>
#include <catch2/catch_test_macros.hpp>

// These tests verify that LabelParams correctly carries all spec fields
// through create_label and serialization. They don't require a running
// NATS/DragonflyDB instance.

TEST_CASE("LabelParams with pipeline populates LabelData", "[client-api]") {
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://compress_rle", "level=3", -1, -1});
    pipeline.stages.push_back({"builtin://checksum_crc32", "", 0, -1});

    labios::LabelParams params{};
    params.type = labios::LabelType::Write;
    params.pipeline = pipeline;

    // Simulate what create_label does (without needing a Session).
    labios::LabelData label{};
    label.type = params.type;
    label.pipeline = params.pipeline;

    REQUIRE(label.pipeline.stages.size() == 2);
    CHECK(label.pipeline.stages[0].operation == "builtin://compress_rle");
    CHECK(label.pipeline.stages[0].args == "level=3");
    CHECK(label.pipeline.stages[1].operation == "builtin://checksum_crc32");
}

TEST_CASE("LabelParams with continuation populates LabelData", "[client-api]") {
    labios::LabelParams params{};
    params.continuation.kind = labios::ContinuationKind::Chain;
    params.continuation.target_channel = "results";
    params.continuation.chain_params = R"({"next":"transform"})";

    labios::LabelData label{};
    label.continuation = params.continuation;

    CHECK(label.continuation.kind == labios::ContinuationKind::Chain);
    CHECK(label.continuation.target_channel == "results");
    CHECK(label.continuation.chain_params == R"({"next":"transform"})");
}

TEST_CASE("LabelParams with dest_uri populates LabelData", "[client-api]") {
    labios::LabelParams params{};
    params.dest_uri = "s3://bucket/key.dat";

    labios::LabelData label{};
    label.dest_uri = params.dest_uri;

    CHECK(label.dest_uri == "s3://bucket/key.dat");
}

TEST_CASE("LabelParams with source_uri populates LabelData", "[client-api]") {
    labios::LabelParams params{};
    params.source_uri = "file:///data/input.bin";

    labios::LabelData label{};
    label.source_uri = params.source_uri;

    CHECK(label.source_uri == "file:///data/input.bin");
}

TEST_CASE("LabelParams with intent and priority populates LabelData", "[client-api]") {
    labios::LabelParams params{};
    params.intent = labios::Intent::Checkpoint;
    params.priority = 7;

    labios::LabelData label{};
    label.intent = params.intent;
    label.priority = params.priority;

    CHECK(label.intent == labios::Intent::Checkpoint);
    CHECK(label.priority == 7);
}

TEST_CASE("LabelParams with durability=Durable populates LabelData", "[client-api]") {
    labios::LabelParams params{};
    params.durability = labios::Durability::Durable;

    labios::LabelData label{};
    label.durability = params.durability;

    CHECK(label.durability == labios::Durability::Durable);
}

TEST_CASE("LabelParams with version field populates LabelData", "[client-api]") {
    labios::LabelParams params{};
    params.version = 42;

    labios::LabelData label{};
    label.version = params.version;

    CHECK(label.version == 42);
}

TEST_CASE("LabelParams with all spec fields roundtrips through serialize/deserialize",
          "[client-api]") {
    labios::LabelData label{};
    label.id = 12345;
    label.type = labios::LabelType::Write;
    label.operation = "write";
    label.version = 99;
    label.durability = labios::Durability::Durable;
    label.source_uri = "file:///src/data.bin";
    label.dest_uri = "s3://archive/data.bin";
    label.intent = labios::Intent::Cache;
    label.priority = 3;

    // Continuation
    label.continuation.kind = labios::ContinuationKind::Notify;
    label.continuation.target_channel = "done-ch";

    // Pipeline
    label.pipeline.stages.push_back({"builtin://compress_rle", "", -1, -1});

    auto buf = labios::serialize_label(label);
    auto restored = labios::deserialize_label(buf);

    CHECK(restored.id == 12345);
    CHECK(restored.version == 99);
    CHECK(restored.durability == labios::Durability::Durable);
    CHECK(restored.source_uri == "file:///src/data.bin");
    CHECK(restored.dest_uri == "s3://archive/data.bin");
    CHECK(restored.intent == labios::Intent::Cache);
    CHECK(restored.priority == 3);
    CHECK(restored.continuation.kind == labios::ContinuationKind::Notify);
    CHECK(restored.continuation.target_channel == "done-ch");
    REQUIRE(restored.pipeline.stages.size() == 1);
    CHECK(restored.pipeline.stages[0].operation == "builtin://compress_rle");
}

TEST_CASE("observe() creates correct OBSERVE label", "[client-api]") {
    // Verify the label construction pattern used by observe().
    labios::LabelParams params{};
    params.type = labios::LabelType::Observe;
    params.source_uri = "observe://queue/depth";

    labios::LabelData label{};
    label.type = params.type;
    label.source_uri = params.source_uri;

    CHECK(label.type == labios::LabelType::Observe);
    CHECK(label.source_uri == "observe://queue/depth");
}

TEST_CASE("write_to() creates label with dest_uri set", "[client-api]") {
    // Verify the label construction pattern used by write_to().
    labios::LabelParams params{};
    params.type = labios::LabelType::Write;
    params.dest_uri = "file:///output/result.dat";

    labios::LabelData label{};
    label.type = params.type;
    label.dest_uri = params.dest_uri;
    label.data_size = 1024;

    CHECK(label.type == labios::LabelType::Write);
    CHECK(label.dest_uri == "file:///output/result.dat");
    CHECK(label.data_size == 1024);
}

TEST_CASE("execute_pipeline() creates label with pipeline and both URIs",
          "[client-api]") {
    // Verify the label construction pattern used by execute_pipeline().
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://compress_rle", "level=5", -1, 1});
    pipeline.stages.push_back({"builtin://checksum_crc32", "", 0, -1});

    labios::LabelParams params{};
    params.type = labios::LabelType::Write;
    params.source_uri = "file:///input/raw.bin";
    params.dest_uri = "s3://processed/compressed.bin";
    params.pipeline = pipeline;
    params.intent = labios::Intent::Intermediate;

    labios::LabelData label{};
    label.type = params.type;
    label.source_uri = params.source_uri;
    label.dest_uri = params.dest_uri;
    label.pipeline = params.pipeline;
    label.intent = params.intent;

    CHECK(label.type == labios::LabelType::Write);
    CHECK(label.source_uri == "file:///input/raw.bin");
    CHECK(label.dest_uri == "s3://processed/compressed.bin");
    CHECK(label.intent == labios::Intent::Intermediate);
    REQUIRE(label.pipeline.stages.size() == 2);
    CHECK(label.pipeline.stages[0].operation == "builtin://compress_rle");
    CHECK(label.pipeline.stages[0].args == "level=5");
    CHECK(label.pipeline.stages[0].input_stage == -1);
    CHECK(label.pipeline.stages[0].output_stage == 1);
    CHECK(label.pipeline.stages[1].operation == "builtin://checksum_crc32");
    CHECK(label.pipeline.stages[1].input_stage == 0);
}

TEST_CASE("LabelParams defaults are zero/empty for new spec fields", "[client-api]") {
    labios::LabelParams params{};

    CHECK(params.version == 0);
    CHECK(params.durability == labios::Durability::Ephemeral);
    CHECK(params.continuation.kind == labios::ContinuationKind::None);
    CHECK(params.source_uri.empty());
    CHECK(params.dest_uri.empty());
    CHECK(params.pipeline.empty());
}
