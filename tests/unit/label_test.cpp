#include <catch2/catch_test_macros.hpp>
#include <labios/label.h>

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
    label.dependencies = {10, 20, 30};
    label.data_size = 4096;
    label.intent = labios::Intent::Checkpoint;
    label.ttl_seconds = 3600;
    label.isolation = labios::Isolation::Agent;
    label.reply_to = "reply.subject.42";

    auto buf = labios::serialize_label(label);
    REQUIRE(!buf.empty());

    auto result = labios::deserialize_label(buf);

    REQUIRE(result.id == 42);
    REQUIRE(result.type == labios::LabelType::Write);
    REQUIRE(result.operation == "write_block");
    REQUIRE(result.flags == (labios::LabelFlags::Queued | labios::LabelFlags::Async));
    REQUIRE(result.priority == 5);
    REQUIRE(result.app_id == 100);
    REQUIRE(result.dependencies == std::vector<uint64_t>{10, 20, 30});
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
