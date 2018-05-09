//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_GREEDYSOLVER_H
#define PORUS_MAIN_GREEDYSOLVER_H


#include "solver.h"
#include "../../common/data_structures.h"

class GreedySolver: public solver<solver_output,solver_input> {
public:
    solver_output solve(solver_input input) override;
};


#endif //PORUS_MAIN_GREEDYSOLVER_H
