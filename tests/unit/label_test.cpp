#include <catch2/catch_test_macros.hpp>
#include <labios/label.h>

#include <unordered_set>

TEST_CASE("Label serialization roundtrip with FilePath", "[label]") {
    labios::LabelData label;
    label.id = 42;
    label.type = labios::LabelType::Write;
    label.source = labios::file_path("/tmp/input.dat", 1024, 4096);
    label.destination = labios::file_path("/tmp/output.dat");
    label.operation = "write_block";
    label.flags = labios::LabelFlags::Queued | labios::LabelFlags::Async;
    label.priority = 5;
    label.app_id = 100;
    label.dependencies = {
        {10, labios::HazardType::RAW},
        {20, labios::HazardType::WAW},
        {30, labios::HazardType::WAR},
    };
    label.data_size = 4096;
    label.intent = labios::Intent::Checkpoint;
    label.ttl_seconds = 3600;
    label.isolation = labios::Isolation::Agent;
    label.reply_to = "reply.subject.42";
    label.file_key = "/tmp/input.dat";
    label.children = {100, 200, 300};

    auto buf = labios::serialize_label(label);
    REQUIRE(!buf.empty());

    auto result = labios::deserialize_label(buf);

    REQUIRE(result.id == 42);
    REQUIRE(result.type == labios::LabelType::Write);
    REQUIRE(result.operation == "write_block");
    REQUIRE(result.flags == (labios::LabelFlags::Queued | labios::LabelFlags::Async));
    REQUIRE(result.priority == 5);
    REQUIRE(result.app_id == 100);
    REQUIRE(result.dependencies.size() == 3);
    REQUIRE(result.dependencies[0].label_id == 10);
    REQUIRE(result.dependencies[0].hazard_type == labios::HazardType::RAW);
    REQUIRE(result.dependencies[1].label_id == 20);
    REQUIRE(result.dependencies[1].hazard_type == labios::HazardType::WAW);
    REQUIRE(result.dependencies[2].hazard_type == labios::HazardType::WAR);
    REQUIRE(result.file_key == "/tmp/input.dat");
    REQUIRE(result.children == std::vector<uint64_t>{100, 200, 300});
    REQUIRE(result.data_size == 4096);
    REQUIRE(result.intent == labios::Intent::Checkpoint);
    REQUIRE(result.ttl_seconds == 3600);
    REQUIRE(result.isolation == labios::Isolation::Agent);
    REQUIRE(result.reply_to == "reply.subject.42");

    auto& src = std::get<labios::FilePath>(result.source);
    REQUIRE(src.path == "/tmp/input.dat");
    REQUIRE(src.offset == 1024);
    REQUIRE(src.length == 4096);

    auto& dst = std::get<labios::FilePath>(result.destination);
    REQUIRE(dst.path == "/tmp/output.dat");
    REQUIRE(dst.offset == 0);
    REQUIRE(dst.length == 0);
}

TEST_CASE("Label serialization roundtrip with MemoryPtr", "[label]") {
    labios::LabelData label;
    label.id = 99;
    label.type = labios::LabelType::Read;

    int dummy = 0;
    label.source = labios::memory_ptr(&dummy, 256);
    label.destination = labios::file_path("/data/out.bin");

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.id == 99);
    REQUIRE(result.type == labios::LabelType::Read);

    auto& src = std::get<labios::MemoryPtr>(result.source);
    REQUIRE(src.address == reinterpret_cast<uint64_t>(&dummy));
    REQUIRE(src.size == 256);

    auto& dst = std::get<labios::FilePath>(result.destination);
    REQUIRE(dst.path == "/data/out.bin");
}

TEST_CASE("Label ID generation produces unique IDs", "[label]") {
    auto id1 = labios::generate_label_id(1);
    auto id2 = labios::generate_label_id(1);
    auto id3 = labios::generate_label_id(2);

    REQUIRE(id1 != id2);
    REQUIRE(id2 != id3);
    REQUIRE(id1 != id3);
}

TEST_CASE("Snowflake IDs are unique across 100K rapid-fire generations", "[label]") {
    std::unordered_set<uint64_t> ids;
    ids.reserve(100000);
    for (int i = 0; i < 100000; ++i) {
        auto id = labios::generate_label_id(1);
        REQUIRE(ids.insert(id).second);
    }
    REQUIRE(ids.size() == 100000);
}

TEST_CASE("Snowflake IDs are monotonically ordered within a single thread", "[label]") {
    uint64_t prev = 0;
    for (int i = 0; i < 10000; ++i) {
        auto id = labios::generate_label_id(1);
        REQUIRE(id > prev);
        prev = id;
    }
}

TEST_CASE("Different app_ids produce different ID ranges", "[label]") {
    // The node_id bits (21-12) encode app_id + random, so with very high
    // probability the IDs will differ in at least the node_id field.
    // We test this by generating sets and checking no overlap.
    std::unordered_set<uint64_t> set_a, set_b;
    for (int i = 0; i < 1000; ++i) {
        set_a.insert(labios::generate_label_id(100));
        set_b.insert(labios::generate_label_id(200));
    }

    // No collisions within each set (redundant with uniqueness test, but explicit).
    REQUIRE(set_a.size() == 1000);
    REQUIRE(set_b.size() == 1000);

    // No overlap between sets.
    for (auto id : set_a) {
        REQUIRE(set_b.find(id) == set_b.end());
    }
}

TEST_CASE("Completion serialization roundtrip", "[label]") {
    labios::CompletionData comp;
    comp.label_id = 777;
    comp.status = labios::CompletionStatus::Error;
    comp.error = "disk full";
    comp.data_key = "warehouse:777";

    auto buf = labios::serialize_completion(comp);
    REQUIRE(!buf.empty());

    auto result = labios::deserialize_completion(buf);

    REQUIRE(result.label_id == 777);
    REQUIRE(result.status == labios::CompletionStatus::Error);
    REQUIRE(result.error == "disk full");
    REQUIRE(result.data_key == "warehouse:777");
}

// ---------------------------------------------------------------------------
// New spec field tests
// ---------------------------------------------------------------------------

TEST_CASE("Observe label type roundtrips", "[label]") {
    labios::LabelData label;
    label.id = 1;
    label.type = labios::LabelType::Observe;
    label.operation = "watch_dir";

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.type == labios::LabelType::Observe);
    REQUIRE(result.operation == "watch_dir");
}

TEST_CASE("Extended intent values roundtrip", "[label]") {
    auto check = [](labios::Intent intent) {
        labios::LabelData label;
        label.id = 1;
        label.intent = intent;
        auto buf = labios::serialize_label(label);
        auto result = labios::deserialize_label(buf);
        REQUIRE(result.intent == intent);
    };

    check(labios::Intent::Embedding);
    check(labios::Intent::ModelWeight);
    check(labios::Intent::KVCache);
    check(labios::Intent::ReasoningTrace);
}

TEST_CASE("Workspace and Global isolation roundtrip", "[label]") {
    auto check = [](labios::Isolation iso) {
        labios::LabelData label;
        label.id = 1;
        label.isolation = iso;
        auto buf = labios::serialize_label(label);
        auto result = labios::deserialize_label(buf);
        REQUIRE(result.isolation == iso);
    };

    check(labios::Isolation::Workspace);
    check(labios::Isolation::Global);
}

TEST_CASE("Durability roundtrip", "[label]") {
    labios::LabelData label;
    label.id = 1;
    label.durability = labios::Durability::Durable;

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.durability == labios::Durability::Durable);

    // Default should be Ephemeral
    labios::LabelData def;
    auto buf2 = labios::serialize_label(def);
    auto result2 = labios::deserialize_label(buf2);
    REQUIRE(result2.durability == labios::Durability::Ephemeral);
}

TEST_CASE("Continuation roundtrip for all kinds", "[label]") {
    SECTION("Notify") {
        labios::LabelData label;
        label.id = 1;
        label.continuation.kind = labios::ContinuationKind::Notify;
        label.continuation.target_channel = "completions.app42";

        auto buf = labios::serialize_label(label);
        auto result = labios::deserialize_label(buf);

        REQUIRE(result.continuation.kind == labios::ContinuationKind::Notify);
        REQUIRE(result.continuation.target_channel == "completions.app42");
    }

    SECTION("Chain") {
        labios::LabelData label;
        label.id = 2;
        label.continuation.kind = labios::ContinuationKind::Chain;
        label.continuation.chain_params = R"({"type":"Read","path":"/out"})";

        auto buf = labios::serialize_label(label);
        auto result = labios::deserialize_label(buf);

        REQUIRE(result.continuation.kind == labios::ContinuationKind::Chain);
        REQUIRE(result.continuation.chain_params == R"({"type":"Read","path":"/out"})");
    }

    SECTION("Conditional") {
        labios::LabelData label;
        label.id = 3;
        label.continuation.kind = labios::ContinuationKind::Conditional;
        label.continuation.condition = "size > 1048576";
        label.continuation.chain_params = R"({"compress":true})";

        auto buf = labios::serialize_label(label);
        auto result = labios::deserialize_label(buf);

        REQUIRE(result.continuation.kind == labios::ContinuationKind::Conditional);
        REQUIRE(result.continuation.condition == "size > 1048576");
        REQUIRE(result.continuation.chain_params == R"({"compress":true})");
    }

    SECTION("None leaves continuation empty") {
        labios::LabelData label;
        label.id = 4;
        // continuation.kind defaults to None

        auto buf = labios::serialize_label(label);
        auto result = labios::deserialize_label(buf);

        REQUIRE(result.continuation.kind == labios::ContinuationKind::None);
        REQUIRE(result.continuation.target_channel.empty());
    }
}

TEST_CASE("RoutingDecision and HopRecord roundtrip", "[label]") {
    labios::LabelData label;
    label.id = 500;
    label.routing.worker_id = 7;
    label.routing.policy = "constraint";
    label.hops = {
        {"shuffler", 1000},
        {"scheduler", 2500},
        {"worker-7", 3200},
    };

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.routing.worker_id == 7);
    REQUIRE(result.routing.policy == "constraint");
    REQUIRE(result.hops.size() == 3);
    REQUIRE(result.hops[0].component == "shuffler");
    REQUIRE(result.hops[0].timestamp_us == 1000);
    REQUIRE(result.hops[1].component == "scheduler");
    REQUIRE(result.hops[1].timestamp_us == 2500);
    REQUIRE(result.hops[2].component == "worker-7");
    REQUIRE(result.hops[2].timestamp_us == 3200);
}

TEST_CASE("StatusCode and timestamps roundtrip", "[label]") {
    labios::LabelData label;
    label.id = 600;
    label.status = labios::StatusCode::Executing;
    label.created_us = 1700000000000000ULL;
    label.completed_us = 0;

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.status == labios::StatusCode::Executing);
    REQUIRE(result.created_us == 1700000000000000ULL);
    REQUIRE(result.completed_us == 0);

    // Complete status with both timestamps
    label.status = labios::StatusCode::Complete;
    label.completed_us = 1700000000050000ULL;

    buf = labios::serialize_label(label);
    result = labios::deserialize_label(buf);

    REQUIRE(result.status == labios::StatusCode::Complete);
    REQUIRE(result.completed_us == 1700000000050000ULL);
}

TEST_CASE("Version field roundtrip", "[label]") {
    labios::LabelData label;
    label.id = 700;
    label.version = 42;

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.version == 42);

    // Default is 0
    labios::LabelData def;
    auto buf2 = labios::serialize_label(def);
    auto result2 = labios::deserialize_label(buf2);
    REQUIRE(result2.version == 0);
}

TEST_CASE("source_uri and dest_uri roundtrip", "[label]") {
    labios::LabelData label;
    label.id = 800;
    label.source_uri = "labios://node-3/scratch/input.h5";
    label.dest_uri = "labios://warehouse/results/output.h5";

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.source_uri == "labios://node-3/scratch/input.h5");
    REQUIRE(result.dest_uri == "labios://warehouse/results/output.h5");

    // Empty by default
    labios::LabelData def;
    auto buf2 = labios::serialize_label(def);
    auto result2 = labios::deserialize_label(buf2);
    REQUIRE(result2.source_uri.empty());
    REQUIRE(result2.dest_uri.empty());
}

TEST_CASE("All new fields default correctly on minimal label", "[label]") {
    labios::LabelData label;
    label.id = 999;
    label.type = labios::LabelType::Write;

    auto buf = labios::serialize_label(label);
    auto result = labios::deserialize_label(buf);

    REQUIRE(result.version == 0);
    REQUIRE(result.durability == labios::Durability::Ephemeral);
    REQUIRE(result.continuation.kind == labios::ContinuationKind::None);
    REQUIRE(result.source_uri.empty());
    REQUIRE(result.dest_uri.empty());
    REQUIRE(result.routing.worker_id == 0);
    REQUIRE(result.routing.policy.empty());
    REQUIRE(result.hops.empty());
    REQUIRE(result.status == labios::StatusCode::Created);
    REQUIRE(result.created_us == 0);
    REQUIRE(result.completed_us == 0);
}
