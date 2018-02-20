//
// Created by anthony on 5/11/17.
//

#include "mdm_factory.h"
#include "posix_mdm.h"
#include "../../common/constants.h"

/******************************************************************************
*Initialization of static members
******************************************************************************/
std::shared_ptr<mdm_factory> mdm_factory::instance = nullptr;
/******************************************************************************
*Gettters and setters
******************************************************************************/
std::shared_ptr<abstract_mdm> mdm_factory::get_mdm(const std::string name) {
  if(mdm_map.empty()){
    mdm_map=std::unordered_map<std::string, std::shared_ptr<abstract_mdm>>();
  }
  auto iter = mdm_map.find(name);
  if(iter != mdm_map.end()) return iter->second;
  else {
    std::shared_ptr<abstract_mdm> mdm_instance;
    if (name == POSIX_MDM) {
      mdm_instance = posix_mdm::getInstance();
    } else return nullptr;
    mdm_map.emplace(name, mdm_instance);
    return mdm_instance;
  }
}


