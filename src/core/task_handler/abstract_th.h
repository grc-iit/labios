//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_ABSTRACT_TH_H
#define PORUS_ABSTRACT_TH_H

#include <vector>
#include "../../common/components/api_response.h"
#include "../../common/components/api_request.h"
template <typename REQUEST=api_request, typename RESPONSE=api_response>
class abstract_th {
public:
  abstract_th(){}

  virtual RESPONSE queue(REQUEST request)=0;

  virtual ~abstract_th(){}
};
typedef abstract_th<api_request,api_response> base_th;
#endif //PORUS_ABSTRACT_TH_H
