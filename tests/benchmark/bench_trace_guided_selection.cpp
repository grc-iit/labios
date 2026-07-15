#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>
#include <labios/sds/types.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Sample {
    std::string profile;
    std::string arm;
    int repetition = 0;
    uint64_t submission_us = 0;
    uint64_t completion_us = 0;
    bool verified = false;
};

bool enabled() {
    const auto* value = std::getenv("LABIOS_BENCH_LIVE");
    return value != nullptr && std::string(value) == "1";
}

std::string env_or(const char* name, std::string fallback) {
    const auto* value = std::getenv(name);
    return value == nullptr ? fallback : std::string(value);
}

labios::Config benchmark_config() {
    labios::Config config;
    if (const auto* value = std::getenv("LABIOS_NATS_URL")) config.nats_url = value;
    if (const auto* value = std::getenv("LABIOS_REDIS_HOST")) config.redis_host = value;
    return config;
}

std::vector<std::byte> payload(size_t size, uint8_t seed) {
    std::vector<std::byte> result(size);
    for (size_t i = 0; i < size; ++i) {
        result[i] = static_cast<std::byte>(static_cast<uint8_t>(seed + i % 251));
    }
    return result;
}

Sample small_hot_metadata(labios::Client& client, std::string_view run_id,
                          std::string_view arm, int repetition) {
    const auto data = payload(4096, 0x11);
    std::vector<labios::PendingIO> pending;
    const auto start = Clock::now();
    for (int i = 0; i < 16; ++i) {
        pending.push_back(client.async_write_to(
            "file:///bench/p10/" + std::string(run_id) + "/small-" +
            std::to_string(i), data));
    }
    const auto submitted = Clock::now();
    for (auto& item : pending) client.wait(item);
    const auto completed = Clock::now();

    bool verified = true;
    for (int i = 0; i < 16; ++i) {
        verified = verified && client.read_from(
            "file:///bench/p10/" + std::string(run_id) + "/small-" +
            std::to_string(i), data.size()) == data;
    }
    return {"small-hot-metadata", std::string(arm), repetition,
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(submitted - start).count()),
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(completed - submitted).count()),
            verified};
}

Sample large_sequential(labios::Client& client, std::string_view run_id,
                        std::string_view arm, int repetition) {
    const auto data = payload(1024 * 1024, 0x42);
    std::vector<labios::PendingIO> pending;
    const auto start = Clock::now();
    for (int i = 0; i < 4; ++i) {
        pending.push_back(client.async_write_to(
            "file:///bench/p10/" + std::string(run_id) + "/large-" +
            std::to_string(i), data));
    }
    const auto submitted = Clock::now();
    for (auto& item : pending) client.wait(item);
    const auto completed = Clock::now();

    bool verified = true;
    for (int i = 0; i < 4; ++i) {
        const auto result = client.read_from(
            "file:///bench/p10/" + std::string(run_id) + "/large-" +
            std::to_string(i), data.size());
        verified = verified && result == data;
    }
    return {"large-sequential", std::string(arm), repetition,
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(submitted - start).count()),
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(completed - submitted).count()),
            verified};
}

Sample mixed_pipeline(labios::Client& client, std::string_view run_id,
                      std::string_view arm, int repetition) {
    const auto source = payload(256 * 1024, 0x73);
    const std::string prefix = "file:///bench/p10/" + std::string(run_id);
    client.write_to(prefix + "/pipeline-source", source);

    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://identity", "", -1, 1});
    const auto start = Clock::now();
    auto pending = client.execute_pipeline(
        prefix + "/pipeline-source",
        "sqlite:///bench_p10_" + std::string(run_id) + "_result", pipeline);
    const auto submitted = Clock::now();
    client.wait(pending);
    const auto completed = Clock::now();
    const auto result = client.read_from(
        "sqlite:///bench_p10_" + std::string(run_id) + "_result", source.size());

    return {"mixed-pipeline", std::string(arm), repetition,
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(submitted - start).count()),
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(completed - submitted).count()),
            result == source};
}

void write_sample(std::ofstream& output, const Sample& sample) {
    output << sample.profile << ',' << sample.arm << ',' << sample.repetition << ','
           << sample.submission_us << ',' << sample.completion_us << ','
           << (sample.verified ? 1 : 0) << '\n';
}

} // namespace

TEST_CASE("P10 trace-guided live selection experiment", "[bench][live][trace][!benchmark]") {
    if (!enabled()) {
        SKIP("set LABIOS_BENCH_LIVE=1 to run the real dispatcher/worker experiment");
    }

    const auto arm = env_or("LABIOS_BENCH_ARM", "baseline");
    const auto run_id = env_or("LABIOS_BENCH_RUN_ID", "p10-fixed-seed");
    const auto output_path = env_or("LABIOS_BENCH_OUTPUT", "p10-trace-guided.csv");
    const int repetitions = std::max(1, std::stoi(env_or("LABIOS_BENCH_REPETITIONS", "5")));
    REQUIRE((arm == "baseline" || arm == "informed"));

    auto client = labios::connect(benchmark_config());
    std::ofstream output(output_path, std::ios::app);
    REQUIRE(output.good());
    if (output.tellp() == 0) {
        output << "profile,arm,repetition,submission_us,completion_us,verified\n";
    }

    for (int repetition = 0; repetition < repetitions; ++repetition) {
        const auto suffix = run_id + "-" + std::to_string(repetition);
        const auto small = small_hot_metadata(client, suffix, arm, repetition);
        const auto large = large_sequential(client, suffix, arm, repetition);
        const auto mixed = mixed_pipeline(client, suffix, arm, repetition);
        write_sample(output, small);
        write_sample(output, large);
        write_sample(output, mixed);
        REQUIRE(small.verified);
        REQUIRE(large.verified);
        REQUIRE(mixed.verified);
    }
}
