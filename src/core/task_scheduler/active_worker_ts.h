//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_ACTIVE_WORKER_TS_H
#define PORUS_ACTIVE_WORKER_TS_H


#include <memory>
#include <vector>
#include "abstract_ts.h"
#include "../../common/components/api_request.h"
#include "../../common/components/api_response.h"

template <typename REQUEST=api_request, typename RESPONSE=api_response>
class active_worker_ts:public abstract_ts<REQUEST,RESPONSE> {
private:
/******************************************************************************
*Private members
******************************************************************************/
  static std::shared_ptr<active_worker_ts<REQUEST,RESPONSE>> instance;
  active_worker_ts():abstract_ts<REQUEST,RESPONSE>(){}
public:
/******************************************************************************
*Gettters and setters
******************************************************************************/
  static std::shared_ptr<active_worker_ts<REQUEST,RESPONSE>> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<active_worker_ts<REQUEST,RESPONSE>>
           (new active_worker_ts<REQUEST,RESPONSE>()) : instance;
  }
  std::vector<RESPONSE> get_distribution(REQUEST request) override {
    auto num_workers=this->solver(request);

    return std::vector<RESPONSE>();
  }
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~active_worker_ts(){}
};
/******************************************************************************
*Initialization of static members
******************************************************************************/
template <typename REQUEST, typename RESPONSE>
std::shared_ptr<active_worker_ts<REQUEST,RESPONSE>>
    active_worker_ts<REQUEST,RESPONSE>::instance = nullptr;

#endif //PORUS_ACTIVE_WORKER_TS_H
