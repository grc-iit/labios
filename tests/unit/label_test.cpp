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
