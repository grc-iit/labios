//
// Created by anthony on 5/11/17.
//

#include <mpi.h>
#include "posix_rh_two_sided.h"
#include "../../common/porus_system.h"
#include "../../common/utils/Timer.h"
#include "../cache_manager/memcached_client.h"
#include "../metadata_manager/posix_mdm.h"
#include "../task_handler/th_factory.h"

/******************************************************************************
*Initialization of static members
******************************************************************************/
std::shared_ptr<posix_rh_two_sided> posix_rh_two_sided::instance = nullptr;
/******************************************************************************
*Functions
******************************************************************************/
posix_api_response posix_rh_two_sided::handle() {
  auto mdm_posix = std::static_pointer_cast<posix_mdm>
      (mdm_factory::getInstance()->get_mdm(POSIX_MDM));
  auto th_instance = std::static_pointer_cast<abstract_th<posix_api_request,
      posix_api_response>>
      (th_factory<posix_api_request, posix_api_response>::getInstance()->get_th(
          config_manager::getInstance()->TASK_HANDLER_MODE));

  while (!request_queue.is_empty()) {
    posix_api_request request;
    request_queue.try_pop(request);
    request.id = buildId(request);
    std::vector<posix_api_response> responses=std::vector<posix_api_response>();
    if(request.size <= MAX_REQUEST_SIZE){
      responses.push_back(th_instance->queue(request));
    }else{
      auto num_subrequests=request.size/MAX_REQUEST_SIZE+1;
      for(auto i=1;i<=num_subrequests;++i){
        auto sub_request=posix_api_request(request);
        if(i<num_subrequests){
          sub_request.size= MAX_REQUEST_SIZE;
        }else{
          sub_request.size=request.size%MAX_REQUEST_SIZE==0
                           ?MAX_REQUEST_SIZE:request.size%MAX_REQUEST_SIZE;
        }
        sub_request.offset=request.offset+(i-1)*MAX_REQUEST_SIZE;
        sub_request.id=buildId(sub_request);
        auto response = th_instance->queue(sub_request);
        responses.push_back(response);
      }
    }
    MPI_Datatype mpi_response_type = porus_system::getInstance()->build_response_dt();
    MPI_Ssend(&responses,
              (int) responses.size(),
              mpi_response_type,
              request.source,
              porus_system::getInstance()->rank,
              porus_system::getInstance()->MPI_COMM_ALL);
  }
  return posix_api_response();
}

posix_api_response posix_rh_two_sided::submit(posix_api_request request) {
  posix_api_response response;
  MPI_Datatype mpi_request_type = porus_system::getInstance()->build_request_dt();
  MPI_Datatype mpi_response_type = porus_system::getInstance()->build_response_dt();
  MPI_Ssend(&request, 1, mpi_request_type, porus_system::getInstance()
                ->get_porus_client
                    (), porus_system::getInstance()->rank,
            porus_system::getInstance()->MPI_COMM_ALL);
  MPI_Status status;
  MPI_Recv(&response, 1, mpi_response_type, porus_system::getInstance()
      ->get_porus_client
          (), MPI_ANY_TAG, porus_system::getInstance()->MPI_COMM_ALL, &status);
  if (request.operation == READ) {
    auto key_exist_lambda = [](std::string x[]) -> bool {
      return !memcached_client::getInstance()->key_exists(x[0], x[1]);
    };
    Timer wait_timer = Timer();
    std::string args[] = {response.id, DATA_SPACE};
    if (wait_timer.execute_until(key_exist_lambda, TIMEOUT, args)) {
      response = memcached_client::getInstance()->get<posix_api_response>(
          response.id, DATA_SPACE);
    }
  }
  return response;
}

posix_api_response posix_rh_two_sided::accept(posix_api_request request) {
  MPI_Datatype mpi_request_type = porus_system::getInstance()->build_request_dt();
  while (true) {
    MPI_Status status;
    MPI_Recv(&request, 1, mpi_request_type, MPI_ANY_SOURCE, MPI_ANY_TAG,
             porus_system::getInstance()->MPI_COMM_ALL, &status);
    request.source = status.MPI_SOURCE;
    request_queue.try_push(request);
    // launch async handle to check queue
    if (!async_handle.valid()) {
      async_handle = std::async(std::launch::async,
                                &posix_rh_two_sided::handle,
                                this);
    }
  }
  return posix_api_response();
}

