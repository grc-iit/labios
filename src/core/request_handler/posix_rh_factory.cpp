#include "posix_rh_factory.h"
#include "posix_rh_two_sided.h"
#include "../../common/constants.h"

/******************************************************************************
*Initialization of static members
******************************************************************************/
std::shared_ptr<posix_rh_factory> posix_rh_factory::instance = nullptr;
/******************************************************************************
*Gettters and setters
******************************************************************************/
std::shared_ptr<posix_rh> posix_rh_factory::get_rh(const std::string name) {
  if(rh_map.empty()){
    rh_map=std::unordered_map<std::string, std::shared_ptr<posix_rh>>();
  }
  auto iter = rh_map.find(name);
  if(iter != rh_map.end()) return iter->second;
  else {
    std::shared_ptr<posix_rh> rh_instance;
    if (name == RH_TWO_SIDED) {
      rh_instance = posix_rh_two_sided::getInstance();
    } else return nullptr;
    rh_map.emplace(name, rh_instance);
    return rh_instance;
  }
}

