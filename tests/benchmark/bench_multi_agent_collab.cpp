#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/workspace.h>
#include <labios/transport/redis.h>

#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool redis_available() {
    // RedisConnection may wait on a network timeout; keep default CI hermetic.
    const char* opt_in = std::getenv("LABIOS_BENCH_REDIS");
    if (!opt_in || std::string_view(opt_in) != "1") return false;
    try {
        const char* host = std::getenv("LABIOS_REDIS_HOST");
        labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
        return redis.connected();
    } catch (...) {
        return false;
    }
}

std::vector<std::byte> make_value(size_t size) {
    std::vector<std::byte> data(size);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + size,
              static_cast<uint8_t>(0));
    return data;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: one-process workspace component
// ---------------------------------------------------------------------------

// This exercises the public Workspace component, but all logical app IDs share
// one process and one WorkspaceRegistry. It is not a multi-agent test.
TEST_CASE("Workspace component: ACL and data roundtrip", "[bench][workspace][component]") {
    if (!redis_available()) SKIP("Redis not available");

    const char* host = std::getenv("LABIOS_REDIS_HOST");
    labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
    labios::WorkspaceRegistry registry(redis);

    auto ws = registry.create("bench_collab_test", /*owner=*/1);
    REQUIRE(ws != nullptr);
    REQUIRE(ws->has_access(1));

    // Grant access to 10 agents
    for (uint32_t agent = 2; agent <= 10; ++agent) {
        ws->grant_access(agent);
        REQUIRE(ws->has_access(agent));
    }

    // Owner puts a key, another agent reads it
    auto value = make_value(1024);
    auto ver = ws->put("shared_key", value, 1);
    REQUIRE(ver > 0);

    auto got = ws->get("shared_key", 2);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 1024);
    REQUIRE(*got == value);

    // Cleanup
    ws->destroy();
    registry.remove("bench_collab_test");
}

// ---------------------------------------------------------------------------
// Component benchmarks (require the workspace's external Redis service)
// ---------------------------------------------------------------------------

TEST_CASE("Workspace component benchmarks", "[bench][workspace][component][!benchmark]") {
    if (!redis_available()) SKIP("Redis not available");

    const char* host = std::getenv("LABIOS_REDIS_HOST");
    labios::transport::RedisConnection redis(host ? host : "localhost", 6379);
    labios::WorkspaceRegistry registry(redis);

    auto ws = registry.create("bench_collab_perf", /*owner=*/1);
    REQUIRE(ws != nullptr);

    for (uint32_t agent = 2; agent <= 10; ++agent) {
        ws->grant_access(agent);
    }

    auto value = make_value(1024);

    BENCHMARK("Workspace component: put 1000 keys") {
        for (int i = 0; i < 1000; ++i) {
            ws->put("key_" + std::to_string(i), value, 1);
        }
        return 1000;
    };

    // Populate keys for read benchmark
    for (int i = 0; i < 1000; ++i) {
        ws->put("rkey_" + std::to_string(i), value, 1);
    }

    BENCHMARK("Workspace component: get 1000 keys") {
        for (int i = 0; i < 1000; ++i) {
            auto v = ws->get("rkey_" + std::to_string(i), 2);
            (void)v;
        }
        return 1000;
    };

    BENCHMARK("Workspace component: ACL check 10000x") {
        int count = 0;
        for (int i = 0; i < 10000; ++i) {
            if (ws->has_access(static_cast<uint32_t>((i % 10) + 1))) ++count;
        }
        return count;
    };

    // Cleanup
    ws->destroy();
    registry.remove("bench_collab_perf");
}
