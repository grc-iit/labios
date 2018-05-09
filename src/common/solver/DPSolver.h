//
// Created by hdevarajan on 5/8/18.
//

#ifndef PORUS_MAIN_DPSOLVER_H
#define PORUS_MAIN_DPSOLVER_H

#include "Solver.h"
#include "../data_structures.h"


class DPSolver : public Solver {
private:
    int* calculate_values(solver_input input,int num_bins);

public:
    DPSolver(Service service):Solver(service){}
    solver_output solve(solver_input input) override;
};


#endif //PORUS_MAIN_DPSOLVER_H
