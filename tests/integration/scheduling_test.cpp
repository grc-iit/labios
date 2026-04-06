#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

#include <cstddef>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

static std::string unique_channel_name() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "integration/error-cont-" + std::to_string(now);
}

TEST_CASE("M3 demo: write 100 labels, read back, verify", "[scheduling]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr size_t label_size = 64 * 1024;  // 64KB
    constexpr int count = 100;

    std::vector<std::byte> data(label_size);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + label_size,
              static_cast<uint8_t>(0));

    for (int i = 0; i < count; ++i) {
        std::string path = "/labios/sched_test_" + std::to_string(i) + ".bin";
        client.write(path, data);
    }

    for (int i = 0; i < count; ++i) {
        std::string path = "/labios/sched_test_" + std::to_string(i) + ".bin";
        auto result = client.read(path, 0, label_size);
        REQUIRE(result.size() == label_size);
        CHECK(std::equal(result.begin(), result.end(), data.begin()));
    }
}

TEST_CASE("Worker error path still fires notify continuation", "[scheduling]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    auto channel_name = unique_channel_name();
    auto* ch = client.create_channel(channel_name, 30);
    REQUIRE(ch != nullptr);

    std::atomic<int> received{0};
    std::mutex comp_mu;
    labios::CompletionData observed{};

    ch->subscribe([&](const labios::ChannelMessage& msg) {
        auto completion = labios::deserialize_completion(msg.data);
        std::lock_guard lock(comp_mu);
        observed = std::move(completion);
        received.fetch_add(1);
    });

    labios::LabelParams params;
    params.type = labios::LabelType::Write;
    params.dest_uri = "nosuch://backend/failure";
    params.continuation.kind = labios::ContinuationKind::Notify;
    params.continuation.target_channel = channel_name;

    auto label = client.create_label(params);
    std::vector<std::byte> payload(64, std::byte{0x5A});

    auto pending = client.publish(label, payload);
    REQUIRE_THROWS(client.wait(pending));

    for (int i = 0; i < 100 && received.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    REQUIRE(received.load() == 1);
    std::lock_guard lock(comp_mu);
    REQUIRE(observed.label_id == label.id);
    REQUIRE(observed.status == labios::CompletionStatus::Error);
    REQUIRE_FALSE(observed.error.empty());
}
