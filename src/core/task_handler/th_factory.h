//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_TH_FACTORY_H
#define PORUS_TH_FACTORY_H


#include "abstract_th.h"
#include <memory>
#include <unordered_map>
#include "async_th.h"
#include "sync_th.h"

template <typename REQUEST=api_request, typename RESPONSE=api_response>
class th_factory {
private:
/******************************************************************************
*Constructor
******************************************************************************/
  th_factory(): th_map(){}
/******************************************************************************
*Private members
******************************************************************************/
  static std::shared_ptr<th_factory<REQUEST,RESPONSE>> instance;
  std::unordered_map<std::string, std::shared_ptr<abstract_th<REQUEST,RESPONSE>>>
      th_map;
public:
/******************************************************************************
*Gettters and setters
******************************************************************************/
  inline static std::shared_ptr<th_factory<REQUEST,RESPONSE>> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<th_factory<REQUEST,RESPONSE>>
        (new th_factory<REQUEST,RESPONSE>()) : instance;
  }

  std::shared_ptr<abstract_th<REQUEST,RESPONSE>> get_th(const std::string name) {
    if(th_map.empty()){
      th_map=std::unordered_map<std::string,
          std::shared_ptr<abstract_th<REQUEST,RESPONSE>>>();
    }
    auto iter = th_map.find(name);
    if(iter != th_map.end()) return iter->second;
    else {
      std::shared_ptr<abstract_th<REQUEST,RESPONSE>> th_instance;
      if (name == SYNC_TH) {
        th_instance = sync_th<REQUEST,RESPONSE>::getInstance();
      }else if (name == ASYNC_TH) {
      } else return nullptr;
      th_map.emplace(name, th_instance);
      return th_instance;
    }
  }
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~th_factory(){}
};
/******************************************************************************
*Initialization of static members
******************************************************************************/
template <typename REQUEST, typename RESPONSE>
std::shared_ptr<th_factory<REQUEST,RESPONSE>> th_factory<REQUEST,RESPONSE>::instance = nullptr;

#endif //PORUS_TH_FACTORY_H
