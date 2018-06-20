//
// Created by hdevarajan on 5/19/18.
//

#ifndef AETRIO_MAIN_RANDOM_SOLVER_H
#define AETRIO_MAIN_RANDOM_SOLVER_H


#include "solver.h"

class random_solver: public solver {
public:
    explicit random_solver(service service) : solver(service) {}

    solver_output solve(solver_input input) override;

};


#endif //AETRIO_MAIN_RANDOM_SOLVER_H
