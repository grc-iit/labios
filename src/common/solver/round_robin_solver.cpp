//
// Created by hdevarajan on 5/19/18.
//

#include "round_robin_solver.h"

std::shared_ptr<round_robin_solver> round_robin_solver::instance = nullptr;

solver_output_dp round_robin_solver::solve(solver_input_dp input) {
    solver_output_dp solver_output_temp(input.num_task);
    std::size_t worker_index=last_worker_index;
    for(auto t=0;t<input.num_task;t++){
        worker_index= (worker_index % MAX_WORKER_COUNT);
        solver_output_temp.solution[t]= static_cast<int>(worker_index + 1);
        worker_index++;
    }
    last_worker_index=worker_index % MAX_WORKER_COUNT;
    return solver_output_temp;
}