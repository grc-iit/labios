//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_POSIX_RH_TWO_SIDED_H
#define PORUS_POSIX_RH_TWO_SIDED_H

#include <memory>
#include "abstract_rh.h"
#include "posix_rh.h"

class posix_rh_two_sided :public posix_rh{
private:
/******************************************************************************
*Variables and members
******************************************************************************/
  static std::shared_ptr<posix_rh_two_sided> instance;
  std::future<posix_api_response> async_handle;

protected:
/******************************************************************************
*Constructor
******************************************************************************/
  posix_rh_two_sided():posix_rh(){}
  posix_api_response handle() override;
public:
  posix_api_response submit(posix_api_request request) override;
  posix_api_response accept(posix_api_request request) override;

/******************************************************************************
*Getters and setters
******************************************************************************/
  inline static std::shared_ptr<posix_rh_two_sided> getInstance(){
    return instance== nullptr ? instance = std::shared_ptr<posix_rh_two_sided>
        (new posix_rh_two_sided()) : instance;
  }
/******************************************************************************
*Destructor
******************************************************************************/
virtual ~posix_rh_two_sided(){}
};


#endif //PORUS_POSIX_RH_TWO_SIDED_H
