//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_ABSTRACT_TS_H
#define PORUS_ABSTRACT_TS_H

#include <vector>
#include "../../common/components/api_request.h"
#include "../../common/components/api_response.h"
#include "../../common/porus_system.h"
#include "../cache_manager/memcached_client.h"
#include <ortools/algorithms/knapsack_solver.h>

using namespace operations_research;
template <typename REQUEST=api_request, typename RESPONSE=api_response>
class abstract_ts {
public:
  virtual std::vector<RESPONSE> get_distribution(REQUEST request)=0;
  virtual int64 solver(REQUEST request){
    KnapsackSolver solver(KnapsackSolver::KNAPSACK_DYNAMIC_PROGRAMMING_SOLVER,
                          "pp_rd");
    if(request.type==api_type::POSIX){
      //calculate the performance according to the request size and pass
      // dimension as a metric to the read or write perfomance of workers
      int dimension = std::log2(request.size) - std::log2(BASE_SIZE_OF_BUCKET);
      dimension=dimension<=0?0:dimension; //if dimension comes back negative
      //if it exceeds the total number of buckets, then performance is
      // roughly the same by using all available workers
      dimension=dimension<TOTAL_NUM_OF_BUCKETS?dimension:TOTAL_NUM_OF_BUCKETS;

      if(request.operation==READ){
        solver.Init(porus_system::getInstance()->workers_read_performance[dimension],
                    porus_system::getInstance()->workers_power_consumption,
                    config_manager::getInstance()->POWER_CAP);
      }else if(request.operation==WRITE){
        solver.Init(porus_system::getInstance()->workers_write_performance[dimension],
                    porus_system::getInstance()->workers_power_consumption,
                    config_manager::getInstance()->POWER_CAP);
      }
    }
    return solver.Solve();
  }
  inline void elect_leader(){

  }
  abstract_ts(){}
  virtual std::shared_ptr<memcached_client> get_scheduler() {
    std::string leader=
        (char*)memcached_client::getInstance()->get(SCHEDULER_LEADER,
                                                         LEADER).data;
    return memcached_client::getInstance
        (memcached_client::build_config_string(leader));
  }

  virtual ~abstract_ts(){}
};

typedef abstract_ts<api_request,api_response> base_ts;

#endif //PORUS_ABSTRACT_TS_H


