#include <catch2/catch_test_macros.hpp>
#include <labios/workspace.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

TEST_CASE("Workspace put/get roundtrip", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-roundtrip", /*owner_app_id=*/1, redis);

    std::vector<std::byte> data(128, static_cast<std::byte>(0xAB));
    uint64_t ver = ws.put("model/weights", data, 1);
    REQUIRE(ver == 1);

    auto result = ws.get("model/weights", 1);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 128);
    REQUIRE((*result)[0] == static_cast<std::byte>(0xAB));

    ws.destroy();
}

TEST_CASE("Workspace preserves zero-length values", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-zero-length", /*owner_app_id=*/1, redis);

    std::vector<std::byte> empty;
    uint64_t ver = ws.put("empty-value", empty, 1);
    REQUIRE(ver == 1);

    auto latest = ws.get("empty-value", 1);
    REQUIRE(latest.has_value());
    REQUIRE(latest->empty());

    auto versioned = ws.get_version("empty-value", 1, 1);
    REQUIRE(versioned.has_value());
    REQUIRE(versioned->empty());

    ws.destroy();
}

TEST_CASE("Workspace versioning returns correct data per version", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-versioning", 1, redis);

    std::vector<std::byte> v1_data(32, static_cast<std::byte>(0x11));
    std::vector<std::byte> v2_data(64, static_cast<std::byte>(0x22));

    uint64_t ver1 = ws.put("results", v1_data, 1);
    uint64_t ver2 = ws.put("results", v2_data, 1);
    REQUIRE(ver1 == 1);
    REQUIRE(ver2 == 2);

    // get() returns the latest
    auto latest = ws.get("results", 1);
    REQUIRE(latest.has_value());
    REQUIRE(latest->size() == 64);

    // get_version() returns specific versions
    auto got_v1 = ws.get_version("results", 1, 1);
    REQUIRE(got_v1.has_value());
    REQUIRE(got_v1->size() == 32);
    REQUIRE((*got_v1)[0] == static_cast<std::byte>(0x11));

    auto got_v2 = ws.get_version("results", 2, 1);
    REQUIRE(got_v2.has_value());
    REQUIRE(got_v2->size() == 64);
    REQUIRE((*got_v2)[0] == static_cast<std::byte>(0x22));

    // Nonexistent version
    auto got_v3 = ws.get_version("results", 99, 1);
    REQUIRE_FALSE(got_v3.has_value());

    ws.destroy();
}

TEST_CASE("Workspace ACL: owner has access, non-owner denied, grant then allowed", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-acl", /*owner=*/10, redis);

    REQUIRE(ws.has_access(10));
    REQUIRE_FALSE(ws.has_access(20));

    // Non-owner put should throw
    std::vector<std::byte> data(16, static_cast<std::byte>(0xCC));
    REQUIRE_THROWS(ws.put("secret", data, 20));
    REQUIRE_THROWS(ws.get("secret", 20));

    // Grant access
    ws.grant_access(20);
    REQUIRE(ws.has_access(20));

    // Now put and get succeed
    uint64_t ver = ws.put("shared", data, 20);
    REQUIRE(ver == 1);
    auto result = ws.get("shared", 20);
    REQUIRE(result.has_value());

    // Cannot revoke owner
    ws.revoke_access(10);
    REQUIRE(ws.has_access(10));

    // Can revoke non-owner
    ws.revoke_access(20);
    REQUIRE_FALSE(ws.has_access(20));
    REQUIRE_THROWS(ws.get("shared", 20));

    ws.destroy();
}

TEST_CASE("Workspace list keys and prefix filter", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-list", 1, redis);

    std::vector<std::byte> data(8, static_cast<std::byte>(0xDD));
    ws.put("data/train", data, 1);
    ws.put("data/test", data, 1);
    ws.put("model/v1", data, 1);

    auto all = ws.list(1);
    REQUIRE(all.size() == 3);

    auto data_only = ws.list("data/", 1);
    REQUIRE(data_only.size() == 2);
    for (const auto& entry : data_only) {
        REQUIRE(entry.key.substr(0, 5) == "data/");
        REQUIRE(entry.version == 1);
        REQUIRE(entry.size == 8);
        REQUIRE(entry.updated_us > 0);
    }

    auto model_only = ws.list("model/", 1);
    REQUIRE(model_only.size() == 1);
    REQUIRE(model_only[0].key == "model/v1");

    ws.destroy();
}

TEST_CASE("Workspace delete removes key from index and warehouse", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-delete", 1, redis);

    std::vector<std::byte> data(16, static_cast<std::byte>(0xEE));
    ws.put("ephemeral", data, 1);
    ws.put("ephemeral", data, 1);  // version 2

    REQUIRE(ws.list(1).size() == 1);

    bool deleted = ws.del("ephemeral", 1);
    REQUIRE(deleted);

    // Key gone from listing
    REQUIRE(ws.list(1).empty());

    // get returns nullopt
    auto result = ws.get("ephemeral", 1);
    REQUIRE_FALSE(result.has_value());

    // Deleting nonexistent key returns false
    bool again = ws.del("nonexistent", 1);
    REQUIRE_FALSE(again);

    ws.destroy();
}

TEST_CASE("Workspace destroy cleans up all warehouse keys", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-destroy-cleanup", 1, redis);

    std::vector<std::byte> data(8, static_cast<std::byte>(0xFF));
    ws.put("a", data, 1);
    ws.put("b", data, 1);

    // Keys exist before destroy
    auto keys_before = redis.scan_keys("labios:ws:test-destroy-cleanup:*");
    REQUIRE_FALSE(keys_before.empty());

    ws.destroy();
    REQUIRE(ws.is_destroyed());

    // All keys cleaned up
    auto keys_after = redis.scan_keys("labios:ws:test-destroy-cleanup:*");
    REQUIRE(keys_after.empty());
}

TEST_CASE("Workspace with TTL sets expiry on keys", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-ttl", 1, redis, /*ttl_seconds=*/300);

    std::vector<std::byte> data(16, static_cast<std::byte>(0x99));
    uint64_t ver = ws.put("expiring", data, 1);
    REQUIRE(ver == 1);

    // Data is retrievable (TTL has not elapsed)
    auto result = ws.get("expiring", 1);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 16);

    ws.destroy();
}

TEST_CASE("WorkspaceRegistry create, get, list, remove", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::WorkspaceRegistry registry(redis);

    auto* ws1 = registry.create("reg-alpha", 1);
    REQUIRE(ws1 != nullptr);
    REQUIRE(ws1->name() == "reg-alpha");
    REQUIRE(ws1->owner() == 1);

    auto* ws2 = registry.create("reg-beta", 2, 60);
    REQUIRE(ws2 != nullptr);

    // Duplicate name returns nullptr
    auto* dup = registry.create("reg-alpha", 3);
    REQUIRE(dup == nullptr);

    // Get by name
    REQUIRE(registry.get("reg-alpha") == ws1);
    REQUIRE(registry.get("reg-beta") == ws2);
    REQUIRE(registry.get("nonexistent") == nullptr);

    // List
    auto names = registry.list();
    REQUIRE(names.size() == 2);

    // Remove
    ws1->destroy();
    registry.remove("reg-alpha");
    REQUIRE(registry.get("reg-alpha") == nullptr);
    REQUIRE(registry.list().size() == 1);

    ws2->destroy();
    registry.remove("reg-beta");
}

TEST_CASE("Concurrent put from two agents with access", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-concurrent", 1, redis);
    ws.grant_access(2);

    std::vector<std::byte> data_a(32, static_cast<std::byte>(0xAA));
    std::vector<std::byte> data_b(32, static_cast<std::byte>(0xBB));

    // Agent 1 writes 10 versions, agent 2 writes 10 versions to same key
    std::thread t1([&] {
        for (int i = 0; i < 10; ++i) {
            ws.put("shared-key", data_a, 1);
        }
    });

    std::thread t2([&] {
        for (int i = 0; i < 10; ++i) {
            ws.put("shared-key", data_b, 2);
        }
    });

    t1.join();
    t2.join();

    // 20 total versions should have been created
    auto entries = ws.list(1);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].key == "shared-key");
    REQUIRE(entries[0].version == 20);

    ws.destroy();
}

TEST_CASE("Workspace get on nonexistent key returns nullopt", "[workspace]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::Workspace ws("test-get-missing", 1, redis);

    auto result = ws.get("no-such-key", 1);
    REQUIRE_FALSE(result.has_value());

    ws.destroy();
}
