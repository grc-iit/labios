//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_MDM_FACTORY_H
#define PORUS_MDM_FACTORY_H

#include <memory>
#include <unordered_map>
#include "abstract_mdm.h"

class mdm_factory {
private:
/******************************************************************************
*Private members
******************************************************************************/
  static std::shared_ptr<mdm_factory> instance;
  std::unordered_map<std::string, std::shared_ptr<abstract_mdm>> mdm_map;

/******************************************************************************
*Constructor
******************************************************************************/
  mdm_factory(){}

public:
/******************************************************************************
*Gettters and setters
******************************************************************************/
  inline static std::shared_ptr<mdm_factory> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<mdm_factory>
        (new mdm_factory()) : instance;
  }
  std::shared_ptr<abstract_mdm> get_mdm(const std::string name);
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~mdm_factory(){}
};


#endif //PORUS_MDM_FACTORY_H
