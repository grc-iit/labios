#include <labios/client.h>
#include <labios/label.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("PendingIO is default constructible", "[client]") {
    labios::PendingIO status;
    CHECK(status.pending.empty());
}

TEST_CASE("LabelParams builds with designated initializers", "[client]") {
    labios::LabelParams params{};
    params.type = labios::LabelType::Write;
    params.source = labios::memory_ptr(nullptr, 0);
    params.destination = labios::file_path("/test/output.dat");
    params.operation = "write";
    params.flags = labios::LabelFlags::Async;
    params.priority = 5;
    params.intent = labios::Intent::Checkpoint;
    CHECK(params.type == labios::LabelType::Write);
    CHECK(params.flags == labios::LabelFlags::Async);
    CHECK(params.priority == 5);
    CHECK(params.intent == labios::Intent::Checkpoint);
}

TEST_CASE("Client is move constructible", "[client]") {
    static_assert(std::is_move_constructible_v<labios::Client>);
    static_assert(std::is_move_assignable_v<labios::Client>);
}
