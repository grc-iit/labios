//
// Created by hdevarajan on 6/6/17.
//

#ifndef PORUS_POSIX_RH_H
#define PORUS_POSIX_RH_H


#include "../../common/components/api_request.h"
#include "../../common/components/api_response.h"
#include "abstract_rh.h"

class posix_rh : public abstract_rh<posix_api_request,posix_api_response> {
protected:
  posix_rh():abstract_rh(){}

  std::string buildId(posix_api_request request) override;
};


#endif //PORUS_POSIX_RH_H
