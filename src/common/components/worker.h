//
// Created by anthony on 6/14/17.
//

#ifndef PORUS_WORKER_H
#define PORUS_WORKER_H
#include <string>
#include <vector>
#include "enumerators.h"

struct worker {
  std::string id;
  std::string ip;
  bool status;
  worker_load load;
  bool data_availability;
  inline float score(std::vector<float> weights){
    if(weights.size()<4) return -1;
    return status*weights[0] +
        (load/4.0)*weights[1] +
        !data_availability*weights[2] +
        data_availability*weights[3];
  }
};
#endif //PORUS_WORKER_H
