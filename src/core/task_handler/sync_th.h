//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_SYNC_TH_H
#define PORUS_SYNC_TH_H

#include <memory>
#include <vector>
#include "../../common/components/api_response.h"
#include "../../common/components/api_request.h"
#include "abstract_th.h"
#include "../metadata_manager/posix_mdm.h"
#include "../../common/utils/Timer.h"
#include "../task_scheduler/abstract_ts.h"
#include "../task_scheduler/ts_factory.h"

template <typename REQUEST=api_request, typename RESPONSE=api_response>
class sync_th : public abstract_th<REQUEST,RESPONSE>{
private:
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<sync_th<REQUEST,RESPONSE>> instance;
  sync_th():abstract_th<REQUEST,RESPONSE>(){};
public:
  RESPONSE queue(REQUEST api_request) override  {
    RESPONSE response;
    if(api_request.type==POSIX) {
      auto mdm_posix = std::static_pointer_cast<posix_mdm>
          (mdm_factory::getInstance()->get_mdm(POSIX_MDM));
      auto ts_instance = std::static_pointer_cast<abstract_ts<posix_api_request,posix_api_response>>
          (ts_factory<posix_api_request,posix_api_response>::getInstance()->get_ts
                                               (config_manager::getInstance()->TS_MODE));
      if(api_request.operation==posix_operation::READ){

        ts_instance->get_scheduler()->append("",api_request.id,
                                                READ_TASK_QUEUE);
      }else if(api_request.operation==posix_operation::WRITE){
        ts_instance->get_scheduler()->append("",api_request.id,
                                                WRITE_TASK_QUEUE);
      }
      auto is_task_completed = [](std::string x[]) -> bool {
        return !memcached_client::getInstance()->key_exists(x[0], x[1]);
      };
      Timer wait_timer = Timer();
      std::string args[] = {api_request.id, COMPLETE_TASK};
      if (!wait_timer.execute_until(is_task_completed, TIMEOUT, args)) {
        response=posix_api_response();
        response.status=REQUEST_FAILED;

      }
    }
    return response;
  }
/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<sync_th<REQUEST,RESPONSE>> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<sync_th>
        (new sync_th<REQUEST,RESPONSE>()) : instance;
  }
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~sync_th(){}

};
/******************************************************************************
*Initialization of static members
******************************************************************************/
template <typename REQUEST, typename RESPONSE>
std::shared_ptr<sync_th<REQUEST,RESPONSE>> sync_th<REQUEST,RESPONSE>::instance = nullptr;




#endif //PORUS_SYNC_TH_H
