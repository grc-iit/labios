//
// Created by hdevarajan on 5/8/18.
//

#ifndef AETRIO_MAIN_SOLVER_H
#define AETRIO_MAIN_SOLVER_H

#include "../data_structures.h"

class solver {
protected:
    service service_i;
public:
    explicit solver(service service) : service_i(service){}
    virtual solver_output solve(solver_input input)=0;
};
#endif //AETRIO_MAIN_SOLVER_H
