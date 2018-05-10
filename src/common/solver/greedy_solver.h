//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_GREEDYSOLVER_H
#define PORUS_MAIN_GREEDYSOLVER_H


#include "solver.h"
#include "../data_structures.h"

class GreedySolver: public solver {
public:
    GreedySolver(service service):solver(service){}
    solver_output solve(solver_input input) override;
};


#endif //PORUS_MAIN_GREEDYSOLVER_H
