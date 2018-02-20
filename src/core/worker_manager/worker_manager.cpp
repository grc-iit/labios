//
// Created by anthony on 6/14/17.
//

#include "worker_manager.h"

worker worker_manager::enrich_worker(worker instance) {
  return worker();
}

std::vector<worker>
worker_manager::initialize_worker(std::vector<std::string> ips) {
  return std::vector<worker>();
}

std::vector<worker> worker_manager::fetch_workers() {
  return std::vector<worker>();
}

api_response worker_manager::update_worker(worker instance) {
  return api_response();
}
