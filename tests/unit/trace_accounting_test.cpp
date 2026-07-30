#include <catch2/catch_test_macros.hpp>
#include <labios/config.h>
#include <labios/telemetry.h>

#include <chrono>
#include <filesystem>

namespace {

labios::WeightProfile tuning() {
    labios::WeightProfile profile;
    profile.trace_alpha = 0.5;
    profile.trace_size_normalization_bytes = 1024.0;
    profile.trace_attempt_ttl_ms = 10;
    return profile;
}

labios::TraceAttempt attempt(uint64_t id, uint32_t number, int worker,
                             std::string scheme, uint64_t bytes,
                             std::chrono::steady_clock::time_point when) {
    return {id, number, worker, std::move(scheme), bytes, 0, when};
}

labios::WorkerInfo snapshot(labios::TelemetryPublisher& telemetry, int worker) {
    std::vector<labios::WorkerInfo> workers(1);
    workers.front().id = worker;
    telemetry.enrich_workers(workers);
    return workers.front();
}

} // namespace

TEST_CASE("trace EWMA initializes from a size-normalized successful sample",
          "[trace][accounting]") {
    labios::TelemetryPublisher telemetry(tuning());
    const auto start = std::chrono::steady_clock::time_point{};
    REQUIRE(telemetry.record_attempt_dispatched(
        attempt(1, 1, 7, "file", 512, start)));
    REQUIRE(telemetry.record_attempt_completed(
        1, 1, true, start + std::chrono::microseconds(100)));

    const auto worker = snapshot(telemetry, 7);
    CHECK(worker.trace_samples == 1);
    CHECK(worker.trace_service_us == 200.0);
    CHECK(worker.trace_throughput_bytes_per_sec == 5'120'000.0);
    CHECK(worker.trace_scheme_throughput.at("file") == 5'120'000.0);
    CHECK(telemetry.inflight_for_worker(7) == 0);
}

TEST_CASE("trace duplicate and out-of-order completions are idempotent",
          "[trace][accounting]") {
    labios::TelemetryPublisher telemetry(tuning());
    const auto start = std::chrono::steady_clock::time_point{};
    CHECK_FALSE(telemetry.record_attempt_completed(2, 1, true, start));
    REQUIRE(telemetry.record_attempt_dispatched(
        attempt(2, 1, 8, "sqlite", 1024, start)));
    CHECK_FALSE(telemetry.record_attempt_dispatched(
        attempt(2, 1, 8, "sqlite", 1024, start)));
    REQUIRE(telemetry.record_attempt_completed(
        2, 1, true, start + std::chrono::microseconds(50)));
    CHECK_FALSE(telemetry.record_attempt_completed(
        2, 1, true, start + std::chrono::microseconds(60)));
    CHECK(snapshot(telemetry, 8).trace_samples == 1);
}

TEST_CASE("trace expiry drains in-flight without a success sample",
          "[trace][accounting]") {
    labios::TelemetryPublisher telemetry(tuning());
    const auto start = std::chrono::steady_clock::time_point{};
    REQUIRE(telemetry.record_attempt_dispatched(
        attempt(3, 4, 9, "file", 1024, start)));
    CHECK(telemetry.expire_attempts(start + std::chrono::milliseconds(9)) == 0);
    CHECK(telemetry.expire_attempts(start + std::chrono::milliseconds(10)) == 1);
    CHECK(telemetry.inflight_for_worker(9) == 0);
    CHECK(snapshot(telemetry, 9).trace_samples == 0);
}

TEST_CASE("failed and zero-byte attempts never enter successful trace EWMAs",
          "[trace][accounting]") {
    labios::TelemetryPublisher telemetry(tuning());
    const auto start = std::chrono::steady_clock::time_point{};
    REQUIRE(telemetry.record_attempt_dispatched(
        attempt(4, 1, 10, "file", 1024, start)));
    REQUIRE(telemetry.record_attempt_completed(
        4, 1, false, start + std::chrono::microseconds(10)));
    REQUIRE(telemetry.record_attempt_dispatched(
        attempt(5, 1, 10, "file", 0, start)));
    REQUIRE(telemetry.record_attempt_completed(
        5, 1, true, start + std::chrono::microseconds(10)));
    const auto worker = snapshot(telemetry, 10);
    CHECK(worker.trace_samples == 0);
    CHECK(worker.trace_service_us == 0.0);
    CHECK(worker.trace_throughput_bytes_per_sec == 0.0);
    CHECK(worker.trace_scheme_throughput.empty());
}

TEST_CASE("requested scheduler profiles fail closed and experiment arms differ",
          "[trace][config]") {
    CHECK_THROWS(labios::load_weight_profile(
        std::filesystem::path("/definitely/missing/labios-profile.toml")));

    const auto repository =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const auto informed = labios::load_weight_profile(
        repository / "conf/profiles/trace_guided.toml");
    const auto ablation = labios::load_weight_profile(
        repository / "conf/profiles/trace_ablation.toml");
    CHECK(informed.trace_service > 0.0);
    CHECK(informed.trace_queue > 0.0);
    CHECK(informed.trace_throughput > 0.0);
    CHECK(ablation.trace_service == 0.0);
    CHECK(ablation.trace_queue == 0.0);
    CHECK(ablation.trace_throughput == 0.0);
    CHECK(informed.availability == ablation.availability);
    CHECK(informed.capacity == ablation.capacity);
    CHECK(informed.load == ablation.load);
    CHECK(informed.speed == ablation.speed);
    CHECK(informed.energy == ablation.energy);
}
