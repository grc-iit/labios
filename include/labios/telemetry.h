#pragma once
#include <labios/solver/solver.h>
#include <labios/transport/nats.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace labios {

/// Snapshot function that returns the current worker list.
/// The dispatcher supplies this so the publisher can read workers
/// without owning the InMemoryWorkerManager directly.
using WorkerSnapshot = std::function<std::vector<WorkerInfo>()>;

/// Publishes continuous telemetry metrics to NATS subject "labios.telemetry".
/// Agents subscribe to this stream for real-time system monitoring.
class TelemetryPublisher {
public:
    TelemetryPublisher(transport::NatsConnection& nats,
                        WorkerSnapshot worker_fn,
                        std::chrono::milliseconds interval = std::chrono::seconds(2));
    ~TelemetryPublisher();

    TelemetryPublisher(const TelemetryPublisher&) = delete;
    TelemetryPublisher& operator=(const TelemetryPublisher&) = delete;

    void start();
    void stop();

    /// Called by the dispatcher when a label is dispatched to a worker.
    void record_label_dispatched();

    /// Called by the dispatcher when a label completion arrives.
    void record_label_completed(std::chrono::microseconds latency);

private:
    transport::NatsConnection& nats_;
    WorkerSnapshot worker_fn_;
    std::chrono::milliseconds interval_;
    std::jthread thread_;

    std::atomic<uint64_t> labels_dispatched_{0};
    std::atomic<uint64_t> labels_completed_{0};
    std::atomic<uint64_t> total_latency_us_{0};

    std::mutex latency_mu_;
    std::vector<uint64_t> latency_samples_;

    void publish_loop(std::stop_token stoken);
};

} // namespace labios
