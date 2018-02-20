//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_API_H
#define PORUS_API_H

#include <memory>

#include <mpi.h>
#include "core/metadata_manager/mdm_factory.h"
#include "core/request_handler/posix_rh_factory.h"

class api {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<api> instance;

/******************************************************************************
*Constructors
******************************************************************************/
  api();
public:
  std::shared_ptr<mdm_factory> factory_mdm;
  std::shared_ptr<posix_rh_factory> factory_rh;
/******************************************************************************
*Destructor
******************************************************************************/
  virtual ~api();
/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<api> getInstance(){
    return instance== nullptr ? instance=std::shared_ptr<api>(new api())
                              : instance;
  }
};


#endif //PORUS_API_H
