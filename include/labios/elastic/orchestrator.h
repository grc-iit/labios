#pragma once

#include <labios/config.h>
#include <labios/elastic/decision_engine.h>
#include <labios/elastic/docker_client.h>
#include <labios/worker_manager.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>

namespace labios::elastic {

template<ContainerRuntime Runtime>
class Orchestrator {
public:
    Orchestrator(InMemoryWorkerManager& mgr, Runtime& runtime, const Config& cfg)
        : mgr_(mgr), runtime_(runtime), cfg_(cfg) {}

    void update_queue_depth(int depth) {
        if (depth >= cfg_.dispatcher_batch_size) {
            pressure_count_.fetch_add(1);
        } else {
            pressure_count_.store(0);
        }
    }

    /// Update from the dispatcher's enriched queue depth message.
    void update_queue_breakdown(const QueueBreakdown& qb) {
        queue_breakdown_.store_total(qb.total);
        queue_breakdown_.store_pipeline(qb.with_pipeline);
        queue_breakdown_.store_observe(qb.observe_count);
        update_queue_depth(qb.total);
    }

    void evaluate_once() {
        auto idle = mgr_.decommissionable_workers(
            std::chrono::milliseconds(cfg_.elastic.decommission_timeout_ms));
        auto suspended = mgr_.suspended_workers();

        std::vector<int> decomm_candidates;
        std::chrono::steady_clock::time_point last_comm;
        {
            std::lock_guard lock(commissioned_mu_);
            for (int wid : idle) {
                if (commissioned_.count(wid)) decomm_candidates.push_back(wid);
            }
            last_comm = last_commission_time_;
        }

        ElasticSnapshot snap{
            .pressure_count = pressure_count_.load(),
            .pressure_threshold = cfg_.elastic.pressure_threshold,
            .current_workers = static_cast<int>(mgr_.worker_count()),
            .min_workers = cfg_.elastic.min_workers,
            .max_workers = cfg_.elastic.max_workers,
            .idle_worker_ids = decomm_candidates,
            .suspended_worker_ids = suspended,
            .last_commission = last_comm,
            .cooldown = std::chrono::milliseconds(cfg_.elastic.commission_cooldown_ms),
        };

        auto decision = evaluate(snap);

        switch (decision.action) {
            case Action::Commission:
                commission_worker();
                break;
            case Action::Decommission:
                decommission_worker(decision.target_worker_id);
                break;
            case Action::Resume:
                pending_resume_id_.store(decision.target_worker_id);
                break;
            case Action::None:
                break;
        }
    }

    /// Tier-aware evaluation using queue breakdown and per-tier state.
    void evaluate_tiered_once() {
        auto timeout = std::chrono::milliseconds(cfg_.elastic.decommission_timeout_ms);

        auto build_tier = [&](WorkerTier tier, int min_w, int max_w) -> TierState {
            TierState ts;
            ts.active = mgr_.count_by_tier(tier);
            ts.min = min_w;
            ts.max = max_w;

            auto idle_all = mgr_.decommissionable_workers_by_tier(tier, timeout);
            {
                std::lock_guard lock(commissioned_mu_);
                for (int wid : idle_all) {
                    if (commissioned_.count(wid)) ts.idle_ids.push_back(wid);
                }
            }
            ts.suspended_ids = mgr_.suspended_workers_by_tier(tier);
            return ts;
        };

        std::chrono::steady_clock::time_point last_comm;
        {
            std::lock_guard lock(commissioned_mu_);
            last_comm = last_commission_time_;
        }

        TieredSnapshot snap{
            .queue = {queue_breakdown_.load_total(),
                      queue_breakdown_.load_pipeline(),
                      queue_breakdown_.load_observe()},
            .pressure_count = pressure_count_.load(),
            .pressure_threshold = cfg_.elastic.pressure_threshold,
            .databot = build_tier(WorkerTier::Databot,
                                  cfg_.elastic.min_databot_workers,
                                  cfg_.elastic.max_databot_workers),
            .pipeline = build_tier(WorkerTier::Pipeline,
                                   cfg_.elastic.min_pipeline_workers,
                                   cfg_.elastic.max_pipeline_workers),
            .agentic = build_tier(WorkerTier::Agentic,
                                  cfg_.elastic.min_agentic_workers,
                                  cfg_.elastic.max_agentic_workers),
            .last_commission = last_comm,
            .cooldown = std::chrono::milliseconds(cfg_.elastic.commission_cooldown_ms),
        };

        auto sd = evaluate_tiered(snap);

        switch (sd.action) {
            case Action::Commission:
                commission_worker(sd.target_tier);
                break;
            case Action::Decommission:
                decommission_worker(sd.target_worker_id);
                break;
            case Action::Resume:
                pending_resume_id_.store(sd.target_worker_id);
                break;
            case Action::None:
                break;
        }
    }

    void run(std::stop_token stoken) {
        auto interval = std::chrono::milliseconds(cfg_.elastic.eval_interval_ms);
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(interval);
            if (stoken.stop_requested()) break;
            evaluate_once();
        }
    }

    void record_commissioned(int worker_id, const std::string& container_id) {
        std::lock_guard lock(commissioned_mu_);
        commissioned_[worker_id] = container_id;
    }

    int consume_pending_resume() {
        return pending_resume_id_.exchange(0);
    }

private:
    void commission_worker(WorkerTier tier = WorkerTier::Databot) {
        int wid = mgr_.next_worker_id();
        int tier_int = static_cast<int>(tier);

        ContainerSpec spec{
            .image = cfg_.elastic.docker_image,
            .name = "labios-elastic-worker-" + std::to_string(wid),
            .env = {
                "LABIOS_NATS_URL=" + cfg_.nats_url,
                "LABIOS_REDIS_HOST=" + cfg_.redis_host,
                "LABIOS_WORKER_ID=" + std::to_string(wid),
                "LABIOS_WORKER_SPEED=" + std::to_string(cfg_.elastic.elastic_worker_speed),
                "LABIOS_WORKER_ENERGY=" + std::to_string(cfg_.elastic.elastic_worker_energy),
                "LABIOS_WORKER_CAPACITY=" + cfg_.elastic.elastic_worker_capacity,
                "LABIOS_CONFIG_PATH=/etc/labios/labios.toml",
                "LABIOS_WORKER_IDLE_TIMEOUT_MS=" +
                    std::to_string(cfg_.elastic.worker_idle_timeout_ms),
                "LABIOS_WORKER_TIER=" + std::to_string(tier_int),
            },
            .network = cfg_.elastic.docker_network,
        };

        try {
            auto container_id = runtime_.create_and_start(spec);
            {
                std::lock_guard lock(commissioned_mu_);
                commissioned_[wid] = container_id;
                last_commission_time_ = std::chrono::steady_clock::now();
            }
            pressure_count_.store(0);

            static constexpr const char* tier_names[] = {"databot", "pipeline", "agentic"};
            std::cout << "[elastic] commissioned " << tier_names[tier_int]
                      << " worker " << wid
                      << " (container " << container_id.substr(0, 12) << ")\n"
                      << std::flush;
        } catch (const std::exception& e) {
            std::cerr << "[elastic] commission failed: " << e.what() << "\n"
                      << std::flush;
        }
    }

    void decommission_worker(int worker_id) {
        std::string container_id;
        {
            std::lock_guard lock(commissioned_mu_);
            auto it = commissioned_.find(worker_id);
            if (it == commissioned_.end()) return;
            container_id = it->second;
            commissioned_.erase(it);
        }

        mgr_.deregister_worker(worker_id);

        try {
            runtime_.stop_and_remove(container_id);
            std::cout << "[elastic] decommissioned worker " << worker_id << "\n"
                      << std::flush;
        } catch (const std::exception& e) {
            std::cerr << "[elastic] decommission failed for worker "
                      << worker_id << ": " << e.what() << "\n" << std::flush;
        }
    }

    /// Atomic wrapper for queue breakdown fields.
    struct AtomicQueueBreakdown {
        void store_total(int v) { total.store(v); }
        void store_pipeline(int v) { with_pipeline.store(v); }
        void store_observe(int v) { observe_count.store(v); }
        int load_total() const { return total.load(); }
        int load_pipeline() const { return with_pipeline.load(); }
        int load_observe() const { return observe_count.load(); }
    private:
        std::atomic<int> total{0};
        std::atomic<int> with_pipeline{0};
        std::atomic<int> observe_count{0};
    };

    InMemoryWorkerManager& mgr_;
    Runtime& runtime_;
    Config cfg_;

    std::atomic<int> pressure_count_{0};
    AtomicQueueBreakdown queue_breakdown_;
    std::chrono::steady_clock::time_point last_commission_time_{};
    std::mutex commissioned_mu_;
    std::unordered_map<int, std::string> commissioned_;
    std::atomic<int> pending_resume_id_{0};
};

} // namespace labios::elastic
