//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_API_REQUEST_H
#define PORUS_API_REQUEST_H

#include "enumerators.h"
#include "../constants.h"

struct api_request {
  std::string id;
  int source;
  api_type type;
  api_request(){}
  api_request(const api_request& other){
    id=other.id;
    source=other.source;
    type=other.type;
  }
  virtual ~api_request() {}
};

struct posix_api_request : public api_request {
  posix_operation operation;
  long offset;
  size_t size;
  int fd;
  posix_api_request():api_request(){}
  posix_api_request(const posix_api_request& other):api_request(other){
    operation=other.operation;
    offset=other.offset;
    size=other.size;
    fd=other.fd;
  }
  virtual ~posix_api_request() {}
};
#endif //PORUS_API_REQUEST_H
