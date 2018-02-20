//
// Created by anthony on 5/17/17.
//

#ifndef PORUS_RESPONSE_H
#define PORUS_RESPONSE_H

#include "../return_codes.h"
#include <string>
struct api_response {
  std::string id;
  int status;
  size_t size;
  void* data;
  api_response(){}

  api_response(int _status) {
    status = _status;
  }
  api_response(std::string _id, int _status, size_t _size){
    id = _id;
    status = _status;
    size = _size;
  }
  virtual ~api_response() {}
};

struct posix_api_response : public api_response {
  FILE* fh;
  int fd;
  posix_api_response():api_response(){}
  posix_api_response(int _status): api_response(_status) {}
  posix_api_response(std::string _id, int _status, size_t _size, int _fd)
      :api_response(_id,_status,_size){
    fd = _fd;
  }
  posix_api_response(int _status, FILE* _fh):api_response(_status){
    fh = _fh;
  }
  virtual ~posix_api_response() {}
};

#endif //PORUS_RESPONSE_H
