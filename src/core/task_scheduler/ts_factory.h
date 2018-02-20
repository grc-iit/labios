//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_TS_FACTORY_H
#define PORUS_TS_FACTORY_H


#include <memory>
#include <unordered_map>
#include "abstract_ts.h"
#include "../../common/constants.h"
#include "active_worker_ts.h"
template <typename REQUEST=api_request, typename RESPONSE=api_response>
class ts_factory {
private:
/******************************************************************************
*Constructor
******************************************************************************/
  ts_factory(): ts_map(){}
/******************************************************************************
*Private members
******************************************************************************/
  static std::shared_ptr<ts_factory> instance;

  std::unordered_map<std::string, std::shared_ptr<abstract_ts<REQUEST,
      RESPONSE>>>
      ts_map;
public:
/******************************************************************************
*Gettters and setters
******************************************************************************/
  inline static std::shared_ptr<ts_factory> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<ts_factory>
        (new ts_factory()) : instance;
  }

  std::shared_ptr<abstract_ts<REQUEST,RESPONSE>> get_ts(const std::string
                                                        name) {
    if(ts_map.empty()){
      ts_map=std::unordered_map<std::string,
          std::shared_ptr<abstract_ts<REQUEST,RESPONSE>>>();
    }
    auto iter = ts_map.find(name);
    if(iter != ts_map.end()) return iter->second;
    else {
      std::shared_ptr<abstract_ts<REQUEST,RESPONSE>> ts_instance;
      if (name == ACTIVE_WORKER_TS) {
        ts_instance = active_worker_ts<REQUEST,RESPONSE>::getInstance();
      }else return nullptr;
      ts_map.emplace(name, ts_instance);
      return ts_instance;
    }
  }
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~ts_factory(){}
};
/******************************************************************************
*Initialization of static members
******************************************************************************/
template <typename REQUEST, typename RESPONSE>
std::shared_ptr<ts_factory<REQUEST,RESPONSE>> ts_factory<REQUEST,RESPONSE>::instance = nullptr;

#endif //PORUS_TS_FACTORY_H
