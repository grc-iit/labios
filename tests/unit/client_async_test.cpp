#include <labios/client.h>
#include <labios/label.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("PendingIO is default constructible", "[client]") {
    labios::PendingIO status;
    CHECK(status.pending.empty());
}

TEST_CASE("LabelParams builds with designated initializers", "[client]") {
    labios::LabelParams params{
        .type = labios::LabelType::Write,
        .source = labios::memory_ptr(nullptr, 0),
        .destination = labios::file_path("/test/output.dat"),
        .operation = "write",
        .flags = labios::LabelFlags::Async,
        .priority = 5,
        .dependencies = {},
        .intent = labios::Intent::Checkpoint,
    };
    CHECK(params.type == labios::LabelType::Write);
    CHECK(params.flags == labios::LabelFlags::Async);
    CHECK(params.priority == 5);
    CHECK(params.intent == labios::Intent::Checkpoint);
}

TEST_CASE("Client is move constructible", "[client]") {
    static_assert(std::is_move_constructible_v<labios::Client>);
    static_assert(std::is_move_assignable_v<labios::Client>);
}
