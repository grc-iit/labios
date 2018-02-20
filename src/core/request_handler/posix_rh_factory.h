//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_RH_FACTORY_H
#define PORUS_RH_FACTORY_H

#include <memory>
#include <unordered_map>
#include "abstract_rh.h"
#include "posix_rh.h"

class posix_rh_factory {
private:
/******************************************************************************
*Constructor
******************************************************************************/
  posix_rh_factory(): rh_map(){}
/******************************************************************************
*Private members
******************************************************************************/
  static std::shared_ptr<posix_rh_factory> instance;
  std::unordered_map<std::string, std::shared_ptr<posix_rh>>
      rh_map;
public:
/******************************************************************************
*Gettters and setters
******************************************************************************/
  inline static std::shared_ptr<posix_rh_factory> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<posix_rh_factory>
        (new posix_rh_factory()) : instance;
  }
  std::shared_ptr<posix_rh> get_rh(const std::string name);
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~posix_rh_factory(){}
};
#endif //PORUS_RH_FACTORY_H
