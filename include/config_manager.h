//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_CONFIG_MANAGER_H
#define PORUS_CONFIG_MANAGER_H


#include <memory>
#include <ortools/base/logging.h>
#include "../src/common/constants.h"

class config_manager {
private:
  static std::shared_ptr<config_manager> instance;
/******************************************************************************
*Constructor
******************************************************************************/
  config_manager(): NUM_RANKS_PER_NODE(8), NUM_NODES(1),
                    config_string( "--SERVER=localhost:11213"
                                   "--SERVER=localhost:11214" ),
                    TOTAL_NUMBER_OF_WORKERS(1), WATTS_PER_WORKER(150),
                    READ_BW_PER_WORKER(700), WRITE_BW_PER_WORKER(350),
                    POWER_CAP({150}),WORKER_IP(){}
public:
  std::string config_string;
  int NUM_RANKS_PER_NODE;
  int NUM_NODES;
  std::string POSIX_REQUEST_MODE=RH_TWO_SIDED;
  std::string TASK_HANDLER_MODE=SYNC_TH;
  std::string TS_MODE=ACTIVE_WORKER_TS;
/******************************************************************************
*System parameters
******************************************************************************/
  std::size_t TOTAL_NUMBER_OF_WORKERS;
  std::vector<std::string> WORKER_IP;
  std::size_t WATTS_PER_WORKER;
  std::size_t READ_BW_PER_WORKER;
  std::size_t WRITE_BW_PER_WORKER;
  std::vector<int64> POWER_CAP;
/******************************************************************************
*Gettters and setters
******************************************************************************/
  inline static std::shared_ptr<config_manager> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<config_manager>
        (new config_manager()) : instance;
  }
  bool isClientRequired(){
    return POSIX_REQUEST_MODE == RH_TWO_SIDED ||
      POSIX_REQUEST_MODE==RH_ONE_SIDED ||
      POSIX_REQUEST_MODE==RH_GLOBAL_SIDED;
  }
};


#endif //PORUS_CONFIG_MANAGER_H
