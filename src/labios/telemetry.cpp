#include <labios/telemetry.h>

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

void TelemetryPublisher::record_label_dispatched() {
    labels_dispatched_.fetch_add(1, std::memory_order_relaxed);
}

void TelemetryPublisher::record_label_completed(std::chrono::microseconds latency) {
    labels_completed_.fetch_add(1, std::memory_order_relaxed);
    total_latency_us_.fetch_add(
        static_cast<uint64_t>(latency.count()), std::memory_order_relaxed);
}

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

        auto workers = worker_fn_();

        auto tp = std::chrono::system_clock::now();
        auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count();

        std::ostringstream oss;
        oss << "{\"timestamp_ms\":" << ts_ms
            << ",\"labels_dispatched\":" << dispatched
            << ",\"labels_completed\":" << completed
            << ",\"avg_latency_us\":" << avg_latency
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
