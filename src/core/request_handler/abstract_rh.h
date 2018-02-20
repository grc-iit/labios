//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_ABSTRACT_RH_H
#define PORUS_ABSTRACT_RH_H

#include <list>
#include <future>
#include "../../common/components/api_response.h"
#include "../../common/components/api_request.h"
#include "../../common/utils/google_tools/concurrent_queue_lock_free/lock_free_buffer_queue.h"

template <typename REQUEST=api_request, typename RESPONSE=api_response>
class abstract_rh {
protected:
  gcl::lock_free_buffer_queue<REQUEST> request_queue;
  abstract_rh():request_queue(MAX_REQUEST_QUEUE_SIZE){}
  virtual std::string buildId(REQUEST request)=0;
public:
  virtual RESPONSE handle() = 0;
  virtual RESPONSE submit(REQUEST request)=0;
  virtual RESPONSE accept(REQUEST request)=0;
};

typedef abstract_rh<api_request,api_response> base_rh;

#endif //PORUS_ABSTRACT_RH_H
