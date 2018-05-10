//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_DPSOLVER_H
#define PORUS_MAIN_DPSOLVER_H

#include "solver.h"
#include "../data_structures.h"


class DPSolver : public solver {
private:
    int* calculate_values(solver_input input,int num_bins);

public:
    DPSolver(Service service):solver(service){}
    solver_output solve(solver_input input) override;
};


#endif //PORUS_MAIN_DPSOLVER_H
