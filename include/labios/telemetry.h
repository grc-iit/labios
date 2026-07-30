#pragma once
#include <labios/solver/solver.h>
#include <labios/transport/nats.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace labios {

/// Snapshot function that returns the current worker list.
/// The dispatcher supplies this so the publisher can read workers
/// without owning the InMemoryWorkerManager directly.
using WorkerSnapshot = std::function<std::vector<WorkerInfo>()>;

/// Trace-derived metrics used by the scheduler. Samples are accumulated from
/// completed labels; no provisioning or worker-registry state is changed.
struct TraceFeatures {
    uint64_t samples = 0;
    double service_us_ewma = 0.0;
    double queue_depth_ewma = 0.0;
    double throughput_bytes_per_sec_ewma = 0.0;
    std::unordered_map<std::string, double> scheme_throughput_bytes_per_sec;
};

struct TraceAttempt {
    uint64_t label_id = 0;
    uint32_t attempt = 0;
    int worker_id = 0;
    std::string scheme;
    uint64_t bytes = 0;
    uint8_t priority = 0;
    std::chrono::steady_clock::time_point dispatched_at;
};

/// Publishes continuous telemetry metrics to NATS subject "labios.telemetry".
/// Agents subscribe to this stream for real-time system monitoring.
class TelemetryPublisher {
public:
    TelemetryPublisher(transport::NatsConnection& nats,
                        WorkerSnapshot worker_fn,
                        std::chrono::milliseconds interval = std::chrono::seconds(2),
                        WeightProfile trace_profile = {});
    /// Hermetic accounting constructor. start() is invalid without a transport.
    explicit TelemetryPublisher(WeightProfile trace_profile);
    ~TelemetryPublisher();

    TelemetryPublisher(const TelemetryPublisher&) = delete;
    TelemetryPublisher& operator=(const TelemetryPublisher&) = delete;

    void start();
    void stop();

    /// Called by the dispatcher when a label is dispatched to a worker.
    /// Priority is used to bucket into lanes: 0=low, 1=medium, 2=high (priority/85).
    void record_label_dispatched(uint8_t priority = 0);

    /// Records a dispatch against a worker so trace queue depth is observable.
    void record_label_dispatched(int worker_id, uint8_t priority);

    /// Called by the dispatcher when a label completion arrives.
    void record_label_completed(std::chrono::microseconds latency, uint8_t priority = 0);

    /// Adds a completed-label sample to the worker/scheme trace. The latency
    /// is dispatch-to-completion, a conservative end-to-end service proxy.
    void record_label_completed(int worker_id, std::string_view scheme,
                                uint64_t bytes, std::chrono::microseconds latency,
                                uint8_t priority = 0);

    /// Attempt-scoped accounting used by the dispatcher. Duplicate dispatches
    /// and completions are idempotent. Failed/cancelled completions drain
    /// in-flight state without entering successful service/throughput EWMAs.
    bool record_attempt_dispatched(TraceAttempt attempt);
    bool record_attempt_completed(uint64_t label_id, uint32_t attempt,
                                  bool successful,
                                  std::chrono::steady_clock::time_point completed_at =
                                      std::chrono::steady_clock::now());
    bool record_attempt_completed(
        uint64_t label_id, bool successful,
        std::chrono::steady_clock::time_point completed_at =
            std::chrono::steady_clock::now());
    size_t expire_attempts(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    uint64_t inflight_for_worker(int worker_id) const;

    /// Applies the latest trace snapshot to a scheduling worker snapshot.
    void enrich_workers(std::vector<WorkerInfo>& workers) const;

    /// Called when an elastic scaling event occurs (commission/decommission).
    void record_scaling_event();

private:
    transport::NatsConnection* nats_;
    WorkerSnapshot worker_fn_;
    std::chrono::milliseconds interval_;
    std::jthread thread_;

    std::atomic<uint64_t> labels_dispatched_{0};
    std::atomic<uint64_t> labels_completed_{0};
    std::atomic<uint64_t> total_latency_us_{0};

    // Per-priority lane counters (0=low, 1=medium, 2=high based on priority/85 bucketing)
    std::array<std::atomic<uint64_t>, 3> lane_dispatched_{};
    std::array<std::atomic<uint64_t>, 3> lane_completed_{};
    std::atomic<uint64_t> scaling_events_{0};

    std::mutex latency_mu_;
    std::vector<uint64_t> latency_samples_;

    mutable std::mutex trace_mu_;
    std::unordered_map<int, TraceFeatures> trace_features_;
    std::unordered_map<int, uint64_t> inflight_by_worker_;
    std::unordered_map<std::string, TraceAttempt> attempts_;
    WeightProfile trace_profile_;

    void publish_loop(std::stop_token stoken);
};

} // namespace labios
