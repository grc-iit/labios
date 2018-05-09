//
// Created by hdevarajan on 5/8/18.
//

#include "DPSolver.h"
#include "knapsack.cpp"
solver_output DPSolver::solve(solver_input input) {
    solver_output solver_output_i;

    for (auto i = 1; i <= MAX_WORKER_COUNT; i++) {
        solver_output solver_output_temp;
        size_t val= static_cast<size_t>(mulknap(input.num_task,
                                                i,
                                                input.task_value,
                                                input.task_size,
                                                solver_output_temp.solution,
                                                input.worker_capacity));
        if(val>solver_output_i.max_value){
            solver_output_i.max_value=val;
            for(auto t=0;t<input.num_task;t++){
                solver_output_i.solution[t]=solver_output_temp.solution[t];
            }
        }
    }
    return solver_output_i;
}