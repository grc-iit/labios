//
// Created by hdevarajan on 5/19/18.
//

#include <random>
#include "random_solver.h"


solver_output random_solver::solve(solver_input input) {
    std::default_random_engine generator;
    std::uniform_int_distribution<int> dist(1, MAX_WORKER_COUNT);
    solver_output solver_output_temp(input.num_task);
    for(auto t=0;t<input.num_task;t++){
        solver_output_temp.solution[t]=
                static_cast<int>(dist(generator) % MAX_WORKER_COUNT + 1);
    }
    return solver_output_temp;
}
