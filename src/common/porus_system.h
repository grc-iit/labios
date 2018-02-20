//
// Created by anthony on 5/17/17.
//

#ifndef PORUS_SYSTEM_H
#define PORUS_SYSTEM_H

#include <mpi.h>
#include <unordered_set>
#include <climits>
#include <vector>
#include <ortools/base/logging.h>
#include "../api.h"
#include "../../include/config_manager.h"
#include "constants.h"

class porus_system {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<porus_system> instance;
  MPI_Datatype mpi_request;
  MPI_Datatype mpi_response;
  bool isRequestBuilt;
  bool isResponseBuilt;
/******************************************************************************
*Constructors
******************************************************************************/
  porus_system():isRequestBuilt(false),isResponseBuilt(false),
                 workers_read_performance(), workers_write_performance(),
                 workers_power_consumption(){}
public:
/******************************************************************************
*Global variables
******************************************************************************/
  MPI_Comm MPI_COMM_ALL; // the original MPI_COMM_WORLD
  MPI_Comm MPI_COMM_APPS; // only application ranks
  MPI_Comm MPI_COMM_PORUS; // only porus_clients
  int groupkey, rank, size;
  std::vector<std::vector<int64>> workers_read_performance;
  std::vector<std::vector<int64>> workers_write_performance;
  std::vector<std::vector<int64>> workers_power_consumption;
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~porus_system(){}

  void build_worker_parameters();

  /******************************************************************************
  *Functions
  ******************************************************************************/
  inline void init(){
    build_request_dt();
    build_response_dt();
    build_worker_parameters();

  }
  inline bool is_client(){
    return  config_manager::getInstance()->isClientRequired() &&
            rank%config_manager::getInstance()->NUM_RANKS_PER_NODE == 0;
  }
  MPI_Datatype build_request_dt(){
    if(!isRequestBuilt){
      isRequestBuilt=true;
      MPI_Datatype  type[3] = {MPI_INT, MPI_INT, MPI_CHAR};
      int blocklen[3] = {1, 1, INT_MAX};
      MPI_Aint disp[3]={0, sizeof(MPI_INT), 2*sizeof(MPI_INT)};
      MPI_Type_struct(6, blocklen, disp, type, &mpi_request);
      MPI_Type_commit(&mpi_request);
    }
    return mpi_request;
  }
  void destroy_request_dt(){
    if(isRequestBuilt){
      MPI_Type_free(&mpi_request);
    }
  }
  MPI_Datatype build_response_dt(){
    if(!isResponseBuilt){
      isRequestBuilt=true;
      MPI_Datatype  type[4] = {MPI_INT, MPI_INT, MPI_INT,MPI_CHAR};
      int blocklen[4] = {1, 1, 1, INT_MAX};
      MPI_Aint disp[4]={0,sizeof(MPI_INT),2*sizeof(MPI_INT),3*sizeof(MPI_INT)};
      MPI_Type_struct(4, blocklen, disp, type, &mpi_response);
      MPI_Type_commit(&mpi_response);
    }
    return mpi_response;
  }
  void destroy_response_dt(){
    if(isResponseBuilt){
      MPI_Type_free(&mpi_response);
    }
  }
/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<porus_system> getInstance(){
    return instance== nullptr ? instance=std::shared_ptr<porus_system>(new porus_system())
                              : instance;
  }
  inline int get_porus_client(){
    auto num_ranks = config_manager::getInstance()->NUM_RANKS_PER_NODE;
    return rank/num_ranks + num_ranks - 1;
  }
  inline std::string get_memcached_server(){
    return std::to_string(get_porus_client()/config_manager::getInstance()->NUM_NODES);
  }
};

#endif //PORUS_SYSTEM_H
