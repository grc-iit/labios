#pragma once
#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/solver/solver.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>
#include <labios/uri.h>

#include <string>
#include <vector>

namespace labios {

/// Result of an observability query, written back onto the label.
struct ObserveResult {
    bool success = true;
    std::string error;
    std::string json_data;
};

/// Handle an OBSERVE label by querying the requested system state.
/// The observe URI determines what to query:
///   observe://queue/depth       -> queue depth from last reported value
///   observe://workers/scores    -> worker scores and availability
///   observe://workers/count     -> worker count by tier
///   observe://system/health     -> NATS and Redis connectivity
///   observe://channels/list     -> active channel names
///   observe://workspaces/list   -> active workspace names
///   observe://config/current    -> current configuration values
///   observe://data/location     -> which worker holds a given file
ObserveResult handle_observe(const URI& query,
                              const std::vector<WorkerInfo>& workers,
                              transport::RedisConnection& redis,
                              transport::NatsConnection& nats,
                              const Config& cfg,
                              CatalogManager& catalog);

} // namespace labios
