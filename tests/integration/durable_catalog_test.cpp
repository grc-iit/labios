#include <labios/catalog_manager.h>
#include <labios/transport/redis.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
std::string redis_host() {
    if (const char* value = std::getenv("LABIOS_REDIS_HOST")) return value;
    return "127.0.0.1";
}

int redis_port() {
    if (const char* value = std::getenv("LABIOS_REDIS_PORT")) return std::stoi(value);
    return 6379;
}

uint64_t now_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<std::byte> bytes(std::string_view value) {
    const auto* begin = reinterpret_cast<const std::byte*>(value.data());
    return {begin, begin + value.size()};
}

labios::LabelData label(uint64_t id) {
    labios::LabelData result;
    result.id = id;
    result.app_id = 17;
    result.type = labios::LabelType::Write;
    result.operation = "core.write";
    result.operation_version = 1;
    result.ir_version = labios::kCurrentIrVersion;
    result.reply_to = "_INBOX.durable-test." + std::to_string(id);
    return result;
}
}

TEST_CASE("atomic admission survives loss of the dispatcher buffer", "[durable][catalog][live]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);
    const auto id = 9'100'000'001ULL;
    redis.del("labios:catalog:" + std::to_string(id));

    auto submitted = label(id);
    catalog.admit(submitted); // producer-side catalog staging
    submitted.reply_to = "_INBOX.durable-test." + std::to_string(id);
    REQUIRE(catalog.durable_handoff(submitted));
    // A new manager instance models a dispatcher that lost all process memory.
    labios::CatalogManager recovered_catalog(redis);
    const auto recovered = recovered_catalog.recoverable_labels(now_ms());
    const auto found = std::find_if(recovered.begin(), recovered.end(),
        [id](const auto& item) { return item.id == id; });
    REQUIRE(found != recovered.end());
    CHECK(found->reply_to == "_INBOX.durable-test." + std::to_string(id));

    recovered_catalog.park(*found, "NO_WORKERS", 1, now_ms() + 1'000, "NO_WORKERS");
    CHECK_FALSE(recovered_catalog.durable_handoff(submitted));
    CHECK(recovered_catalog.get_status(id) == labios::LabelStatus::Parked);
}

TEST_CASE("unknown dependency parks without throwing", "[durable][catalog][live]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);
    auto dependent = label(9'100'000'002ULL);
    dependent.dependencies.push_back({9'199'999'999ULL, labios::HazardType::RAW});
    const auto readiness = catalog.dependency_readiness(dependent);
    CHECK_FALSE(readiness.ready);
    CHECK(readiness.reason == "UNKNOWN_DEPENDENCY");

    catalog.admit(dependent);
    catalog.park(dependent, readiness.reason, 1, now_ms() + 1'000, readiness.reason);
    const auto key = "labios:catalog:" + std::to_string(dependent.id);
    CHECK(redis.hget(key, "park_reason") == "UNKNOWN_DEPENDENCY");
    CHECK(redis.hget(key, "park_attempts") == "1");
    CHECK(redis.hget(key, "last_error") == "UNKNOWN_DEPENDENCY");
}

TEST_CASE("parked Composite rehydrates the same ordered child program", "[durable][catalog][composite][live]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);
    auto first = label(9'100'000'011ULL);
    auto second = label(9'100'000'012ULL);
    auto parent = label(9'100'000'010ULL);
    parent.type = labios::LabelType::Composite;
    parent.operation = "core.composite";
    parent.children = {first.id, second.id};

    catalog.admit(first);
    catalog.admit(second);
    std::vector<std::vector<std::byte>> program{
        labios::serialize_label(first), labios::serialize_label(second)};
    catalog.persist_composite(parent, program);
    catalog.park(parent, "NO_WORKERS", 1, now_ms(), "NO_WORKERS");

    const auto recovered = catalog.recoverable_labels(now_ms());
    CHECK(std::count_if(recovered.begin(), recovered.end(),
        [&](const auto& item) { return item.id == parent.id; }) == 1);
    CHECK(std::none_of(recovered.begin(), recovered.end(),
        [&](const auto& item) { return item.id == first.id || item.id == second.id; }));
    const auto children = catalog.get_composite_program(parent.id);
    REQUIRE(children.size() == 2);
    CHECK(labios::deserialize_label(children[0]).id == first.id);
    CHECK(labios::deserialize_label(children[1]).id == second.id);
    CHECK(labios::deserialize_label(children[0]).reply_to == first.reply_to);
}

TEST_CASE("malformed catalog snapshots are isolated during recovery", "[durable][catalog][live]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);
    const auto bad_id = 9'100'000'020ULL;
    const auto good_id = 9'100'000'021ULL;
    const auto bad_key = "labios:catalog:" + std::to_string(bad_id);
    redis.del(bad_key);
    const std::vector<labios::transport::RedisConnection::HashField> fields{
        {"status", bytes("queued")},
        {"canonical_label", {std::byte{0x01}, std::byte{0x02}}}};
    redis.hset_fields(bad_key, fields);
    catalog.admit(label(good_id));

    const auto recovered = catalog.recoverable_labels(now_ms());
    CHECK(std::none_of(recovered.begin(), recovered.end(),
        [bad_id](const auto& item) { return item.id == bad_id; }));
    CHECK(std::any_of(recovered.begin(), recovered.end(),
        [good_id](const auto& item) { return item.id == good_id; }));
}
