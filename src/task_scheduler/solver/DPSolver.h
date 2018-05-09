//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_DPSOLVER_H
#define PORUS_MAIN_DPSOLVER_H

#include "solver.h"
#include "../../common/data_structures.h"


class DPSolver : public solver<solver_output, solver_input> {

public:
    solver_output solve(solver_input input) override;
};


#endif //PORUS_MAIN_DPSOLVER_H
