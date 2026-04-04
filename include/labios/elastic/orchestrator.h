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
    void commission_worker() {
        int wid = mgr_.next_worker_id();

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

            std::cout << "[elastic] commissioned worker " << wid
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

    InMemoryWorkerManager& mgr_;
    Runtime& runtime_;
    Config cfg_;

    std::atomic<int> pressure_count_{0};
    std::chrono::steady_clock::time_point last_commission_time_{};
    std::mutex commissioned_mu_;
    std::unordered_map<int, std::string> commissioned_;
    std::atomic<int> pending_resume_id_{0};
};

} // namespace labios::elastic
