//
// Created by hdevarajan on 5/19/18.
//

#include "random_solver.h"


solver_output random_solver::solve(solver_input input) {

    solver_output solver_output_temp(input.num_task);
    for(auto t=0;t<input.num_task;t++){
        solver_output_temp.solution[t]= static_cast<int>((rand() % MAX_WORKER_COUNT) + 1);
    }
    return solver_output_temp;
}
