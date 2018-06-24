//
// Created by anthony on 6/8/18.
//

#ifndef AETRIO_DEFAULT_SOLVER_H
#define AETRIO_DEFAULT_SOLVER_H


#include "solver.h"

class default_solver: public solver {
public:
    explicit default_solver(service service);

    solver_output_dp solve(solver_input_dp input) override;
};


#endif //AETRIO_DEFAULT_SOLVER_H
