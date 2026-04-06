#include <labios/telemetry.h>

#include <algorithm>
#include <chrono>
#include <sstream>

namespace labios {

TelemetryPublisher::TelemetryPublisher(transport::NatsConnection& nats,
                                         WorkerSnapshot worker_fn,
                                         std::chrono::milliseconds interval)
    : nats_(nats), worker_fn_(std::move(worker_fn)), interval_(interval) {}

TelemetryPublisher::~TelemetryPublisher() { stop(); }

void TelemetryPublisher::start() {
    thread_ = std::jthread([this](std::stop_token stoken) {
        publish_loop(stoken);
    });
}

void TelemetryPublisher::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

// Map priority [0,255] to 3 telemetry lanes: low/medium/high.
static constexpr int kNumLanes = 3;
static constexpr int kLaneDivisor = 256 / kNumLanes;  // ~85

void TelemetryPublisher::record_label_dispatched(uint8_t priority) {
    labels_dispatched_.fetch_add(1, std::memory_order_relaxed);
    int lane = std::min(static_cast<int>(priority / kLaneDivisor), kNumLanes - 1);
    lane_dispatched_[lane].fetch_add(1, std::memory_order_relaxed);
}

void TelemetryPublisher::record_label_completed(std::chrono::microseconds latency,
                                                 uint8_t priority) {
    labels_completed_.fetch_add(1, std::memory_order_relaxed);
    int lane = std::min(static_cast<int>(priority / kLaneDivisor), kNumLanes - 1);
    lane_completed_[lane].fetch_add(1, std::memory_order_relaxed);
    total_latency_us_.fetch_add(
        static_cast<uint64_t>(latency.count()), std::memory_order_relaxed);
    {
        std::lock_guard lock(latency_mu_);
        latency_samples_.push_back(static_cast<uint64_t>(latency.count()));
    }
}

void TelemetryPublisher::record_scaling_event() {
    scaling_events_.fetch_add(1, std::memory_order_relaxed);
}

namespace {
uint64_t percentile(std::vector<uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    size_t idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}
} // namespace

void TelemetryPublisher::publish_loop(std::stop_token stoken) {
    while (!stoken.stop_requested()) {
        // Sleep in small increments so stop_requested is checked promptly.
        auto deadline = std::chrono::steady_clock::now() + interval_;
        while (std::chrono::steady_clock::now() < deadline) {
            if (stoken.stop_requested()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stoken.stop_requested()) return;

        // Atomically snapshot and reset counters.
        uint64_t dispatched = labels_dispatched_.exchange(0, std::memory_order_relaxed);
        uint64_t completed = labels_completed_.exchange(0, std::memory_order_relaxed);
        uint64_t latency_us = total_latency_us_.exchange(0, std::memory_order_relaxed);
        uint64_t avg_latency = (completed > 0) ? (latency_us / completed) : 0;
        uint64_t scaling = scaling_events_.exchange(0, std::memory_order_relaxed);
        std::array<uint64_t, 3> ld, lc;
        for (int i = 0; i < 3; ++i) {
            ld[i] = lane_dispatched_[i].exchange(0, std::memory_order_relaxed);
            lc[i] = lane_completed_[i].exchange(0, std::memory_order_relaxed);
        }

        std::vector<uint64_t> samples;
        {
            std::lock_guard lock(latency_mu_);
            samples.swap(latency_samples_);
        }
        std::sort(samples.begin(), samples.end());
        uint64_t p50 = percentile(samples, 0.50);
        uint64_t p95 = percentile(samples, 0.95);
        uint64_t p99 = percentile(samples, 0.99);

        auto workers = worker_fn_();

        auto tp = std::chrono::system_clock::now();
        auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count();

        std::ostringstream oss;
        oss << "{\"timestamp_ms\":" << ts_ms
            << ",\"labels_dispatched\":" << dispatched
            << ",\"labels_completed\":" << completed
            << ",\"avg_latency_us\":" << avg_latency
            << ",\"latency_p50_us\":" << p50
            << ",\"latency_p95_us\":" << p95
            << ",\"latency_p99_us\":" << p99
            << ",\"lane_dispatched\":[" << ld[0] << "," << ld[1] << "," << ld[2] << "]"
            << ",\"lane_completed\":[" << lc[0] << "," << lc[1] << "," << lc[2] << "]"
            << ",\"scaling_events\":" << scaling
            << ",\"worker_count\":" << workers.size()
            << ",\"worker_utilization\":[";

        for (size_t i = 0; i < workers.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"id\":" << workers[i].id
                << ",\"tier\":" << static_cast<int>(workers[i].tier)
                << ",\"load\":" << workers[i].load
                << ",\"available\":" << (workers[i].available ? "true" : "false")
                << "}";
        }
        oss << "]}";

        try {
            nats_.publish("labios.telemetry", oss.str());
            nats_.flush();
        } catch (...) {
            // Best-effort telemetry. Failures are non-fatal.
        }
    }
}

} // namespace labios
