#include <labios/telemetry.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace labios {

TelemetryPublisher::TelemetryPublisher(transport::NatsConnection& nats,
                                         WorkerSnapshot worker_fn,
                                         std::chrono::milliseconds interval,
                                         WeightProfile trace_profile)
    : nats_(&nats), worker_fn_(std::move(worker_fn)), interval_(interval),
      trace_profile_(std::move(trace_profile)) {}

TelemetryPublisher::TelemetryPublisher(WeightProfile trace_profile)
    : nats_(nullptr), interval_(std::chrono::seconds(2)),
      trace_profile_(std::move(trace_profile)) {}

TelemetryPublisher::~TelemetryPublisher() { stop(); }

void TelemetryPublisher::start() {
    if (nats_ == nullptr) {
        throw std::logic_error("telemetry publisher has no transport");
    }
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

void TelemetryPublisher::record_label_dispatched(int worker_id, uint8_t priority) {
    record_label_dispatched(priority);
    std::lock_guard lock(trace_mu_);
    auto& trace = trace_features_[worker_id];
    const auto inflight = ++inflight_by_worker_[worker_id];
    constexpr double alpha = 0.2;
    trace.queue_depth_ewma = trace.samples == 0
        ? static_cast<double>(inflight)
        : (alpha * static_cast<double>(inflight) +
           (1.0 - alpha) * trace.queue_depth_ewma);
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

void TelemetryPublisher::record_label_completed(
    int worker_id, std::string_view scheme, uint64_t bytes,
    std::chrono::microseconds latency, uint8_t priority) {
    record_label_completed(latency, priority);
    std::lock_guard lock(trace_mu_);
    auto& trace = trace_features_[worker_id];
    trace.samples++;
    const auto service_us = static_cast<double>(std::max<int64_t>(1, latency.count()));
    constexpr double alpha = 0.2;
    trace.service_us_ewma = trace.samples == 1
        ? service_us
        : alpha * service_us + (1.0 - alpha) * trace.service_us_ewma;
    const auto inflight_it = inflight_by_worker_.find(worker_id);
    if (inflight_it != inflight_by_worker_.end() && inflight_it->second > 0) {
        --inflight_it->second;
    }
    const auto inflight = inflight_it == inflight_by_worker_.end()
        ? 0ULL : inflight_it->second;
    trace.queue_depth_ewma = trace.samples == 1
        ? static_cast<double>(inflight)
        : alpha * static_cast<double>(inflight) +
          (1.0 - alpha) * trace.queue_depth_ewma;
    const auto throughput = bytes == 0 ? 0.0
        : static_cast<double>(bytes) * 1'000'000.0 / service_us;
    trace.throughput_bytes_per_sec_ewma = trace.samples == 1
        ? throughput
        : alpha * throughput + (1.0 - alpha) * trace.throughput_bytes_per_sec_ewma;
    if (!scheme.empty()) {
        auto& scheme_rate = trace.scheme_throughput_bytes_per_sec[std::string(scheme)];
        scheme_rate = trace.samples == 1 || scheme_rate == 0.0
            ? throughput : alpha * throughput + (1.0 - alpha) * scheme_rate;
    }
}

namespace {
std::string attempt_key(uint64_t label_id, uint32_t attempt) {
    return std::to_string(label_id) + ":" + std::to_string(attempt);
}
}

bool TelemetryPublisher::record_attempt_dispatched(TraceAttempt attempt) {
    std::lock_guard lock(trace_mu_);
    const auto key = attempt_key(attempt.label_id, attempt.attempt);
    if (attempts_.contains(key)) return false;
    attempts_.emplace(key, attempt);
    const auto inflight = ++inflight_by_worker_[attempt.worker_id];
    auto& trace = trace_features_[attempt.worker_id];
    const auto observed = static_cast<double>(inflight);
    trace.queue_depth_ewma = trace.queue_depth_ewma == 0.0
        ? observed
        : trace_profile_.trace_alpha * observed +
          (1.0 - trace_profile_.trace_alpha) * trace.queue_depth_ewma;
    record_label_dispatched(attempt.priority);
    return true;
}

bool TelemetryPublisher::record_attempt_completed(
    uint64_t label_id, uint32_t attempt, bool successful,
    std::chrono::steady_clock::time_point completed_at) {
    TraceAttempt context;
    {
        std::lock_guard lock(trace_mu_);
        const auto key = attempt_key(label_id, attempt);
        const auto it = attempts_.find(key);
        if (it == attempts_.end()) return false;
        context = it->second;
        attempts_.erase(it);
        auto& inflight = inflight_by_worker_[context.worker_id];
        if (inflight > 0) --inflight;
        auto& trace = trace_features_[context.worker_id];
        const auto queue = static_cast<double>(inflight);
        trace.queue_depth_ewma = trace_profile_.trace_alpha * queue +
            (1.0 - trace_profile_.trace_alpha) * trace.queue_depth_ewma;
        if (successful && context.bytes != 0) {
            const auto latency = std::max<int64_t>(
                1, std::chrono::duration_cast<std::chrono::microseconds>(
                       completed_at - context.dispatched_at).count());
            const auto normalized_service =
                static_cast<double>(latency) *
                trace_profile_.trace_size_normalization_bytes /
                static_cast<double>(context.bytes);
            const auto throughput =
                static_cast<double>(context.bytes) * 1'000'000.0 /
                static_cast<double>(latency);
            ++trace.samples;
            const auto update = [&](double prior, double value) {
                return trace.samples == 1 ? value :
                    trace_profile_.trace_alpha * value +
                    (1.0 - trace_profile_.trace_alpha) * prior;
            };
            trace.service_us_ewma =
                update(trace.service_us_ewma, normalized_service);
            trace.throughput_bytes_per_sec_ewma =
                update(trace.throughput_bytes_per_sec_ewma, throughput);
            if (!context.scheme.empty()) {
                auto& rate =
                    trace.scheme_throughput_bytes_per_sec[context.scheme];
                rate = update(rate, throughput);
            }
            record_label_completed(std::chrono::microseconds(latency),
                                   context.priority);
        }
    }
    return true;
}

bool TelemetryPublisher::record_attempt_completed(
    uint64_t label_id, bool successful,
    std::chrono::steady_clock::time_point completed_at) {
    uint32_t latest = 0;
    bool found = false;
    {
        std::lock_guard lock(trace_mu_);
        for (const auto& [key, attempt] : attempts_) {
            (void)key;
            if (attempt.label_id == label_id &&
                (!found || attempt.attempt > latest)) {
                latest = attempt.attempt;
                found = true;
            }
        }
    }
    return found &&
        record_attempt_completed(label_id, latest, successful, completed_at);
}

size_t TelemetryPublisher::expire_attempts(
    std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(trace_mu_);
    const auto ttl =
        std::chrono::milliseconds(trace_profile_.trace_attempt_ttl_ms);
    size_t expired = 0;
    for (auto it = attempts_.begin(); it != attempts_.end();) {
        if (now - it->second.dispatched_at < ttl) {
            ++it;
            continue;
        }
        auto& inflight = inflight_by_worker_[it->second.worker_id];
        if (inflight > 0) --inflight;
        it = attempts_.erase(it);
        ++expired;
    }
    return expired;
}

uint64_t TelemetryPublisher::inflight_for_worker(int worker_id) const {
    std::lock_guard lock(trace_mu_);
    const auto it = inflight_by_worker_.find(worker_id);
    return it == inflight_by_worker_.end() ? 0 : it->second;
}

void TelemetryPublisher::enrich_workers(std::vector<WorkerInfo>& workers) const {
    std::lock_guard lock(trace_mu_);
    for (auto& worker : workers) {
        const auto it = trace_features_.find(worker.id);
        if (it == trace_features_.end()) continue;
        worker.trace_samples = it->second.samples;
        worker.trace_service_us = it->second.service_us_ewma;
        worker.trace_queue_depth = it->second.queue_depth_ewma;
        worker.trace_throughput_bytes_per_sec = it->second.throughput_bytes_per_sec_ewma;
        worker.trace_scheme_throughput = it->second.scheme_throughput_bytes_per_sec;
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

        auto workers = worker_fn_ ? worker_fn_() : std::vector<WorkerInfo>{};
        enrich_workers(workers);

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
        oss << "],\"trace_workers\":[";
        for (size_t i = 0; i < workers.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"id\":" << workers[i].id
                << ",\"samples\":" << workers[i].trace_samples
                << ",\"service_us_ewma\":" << workers[i].trace_service_us
                << ",\"queue_depth_ewma\":" << workers[i].trace_queue_depth
                << ",\"throughput_bytes_per_sec_ewma\":"
                << workers[i].trace_throughput_bytes_per_sec
                << ",\"scheme_throughput_bytes_per_sec\":{";
            size_t scheme_index = 0;
            for (const auto& [scheme, throughput] : workers[i].trace_scheme_throughput) {
                if (scheme_index++ > 0) oss << ",";
                oss << "\"" << scheme << "\":" << throughput;
            }
            oss << "}}";
        }
        oss << "]}";

        try {
            nats_->publish("labios.telemetry", oss.str());
            nats_->flush();
        } catch (...) {
            // Best-effort telemetry. Failures are non-fatal.
        }
    }
}

} // namespace labios
