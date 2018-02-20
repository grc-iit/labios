//
// Created by hdevarajan on 6/6/17.
//

#include "posix_rh.h"

std::string posix_rh::buildId(posix_api_request request) {

  return  std::to_string(request.source)+
          SEPARATOR +
          std::to_string(request.operation) +
          SEPARATOR +
          std::to_string(request.fd) +
          SEPARATOR +
          std::to_string(request.offset) +
          SEPARATOR +
          std::to_string(request.size);
}
