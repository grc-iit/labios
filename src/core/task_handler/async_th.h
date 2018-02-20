//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_ASYNC_TH_H
#define PORUS_ASYNC_TH_H

#include <memory>
#include "../../common/components/api_response.h"
#include "../../common/components/api_request.h"
#include "abstract_th.h"
#include "../metadata_manager/posix_mdm.h"
template <typename REQUEST=api_request, typename RESPONSE=api_response>
class async_th : public abstract_th<REQUEST,RESPONSE>{
private:
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<async_th<REQUEST,RESPONSE>> instance;
public:
private:
  async_th(): abstract_th<REQUEST,RESPONSE>(){}
  std::vector<RESPONSE> submit(REQUEST &request) override {
    return std::vector<api_response>();
  }

public:
/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<async_th<REQUEST,RESPONSE>> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<async_th<REQUEST,RESPONSE>>
        (new async_th<REQUEST,RESPONSE>()) : instance;
  }
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~async_th(){}

};
/******************************************************************************
*Initialization of static members
******************************************************************************/
template <typename REQUEST, typename RESPONSE>
std::shared_ptr<async_th<REQUEST,RESPONSE>> async_th<REQUEST,RESPONSE>::instance = nullptr;


#endif //PORUS_ASYNC_TH_H
