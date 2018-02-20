//
// Created by anthony on 5/19/17.
//

#include "porus_system.h"
/******************************************************************************
*Initialization of static
******************************************************************************/
std::shared_ptr<porus_system> porus_system::instance = nullptr;

void porus_system::build_worker_parameters() {
  auto config=config_manager::getInstance();
  //TODO: read the hostfile and build the vector of worker ips
  config->WORKER_IP.push_back("localhost");

  std::vector<int64> power_bucket=std::vector<int64>();
  //TODO:read from file with power consumption per worker
  for(auto i=1; i<=config->TOTAL_NUMBER_OF_WORKERS;++i){
    power_bucket.push_back(i*config->WATTS_PER_WORKER);
    std::vector<int64> read_bucket=std::vector<int64>();
    std::vector<int64> write_bucket=std::vector<int64>();
    for(auto j=0; j<TOTAL_NUM_OF_BUCKETS;++j){
      read_bucket.push_back(i*config->READ_BW_PER_WORKER);
      write_bucket.push_back(i*config->WRITE_BW_PER_WORKER);
    }
    workers_read_performance.push_back(read_bucket);
    workers_write_performance.push_back(write_bucket);
  }
  workers_power_consumption.push_back(power_bucket);
}

