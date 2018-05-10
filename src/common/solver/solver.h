//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_SOLVER_H
#define PORUS_MAIN_SOLVER_H
#include "../data_structures.h"
class solver {
protected:
    Service service;
public:
    solver(Service service):service(service){}
    virtual solver_output solve(solver_input input)=0;
};
#endif //PORUS_MAIN_SOLVER_H
