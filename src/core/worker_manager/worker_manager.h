//
// Created by anthony on 6/14/17.
//

#ifndef PORUS_WORKER_MANAGER_H
#define PORUS_WORKER_MANAGER_H


#include "../../common/components/worker.h"
#include "../../common/components/api_response.h"

class worker_manager {
  std::vector<worker> initialize_worker(std::vector<std::string> ips);
  worker enrich_worker(worker instance);
  std::vector<worker> fetch_workers();
  api_response update_worker(worker instance);
};


#endif //PORUS_WORKER_MANAGER_H
