//
// Created by hdevarajan on 5/8/18.
//

#include "dp_solver.h"
#include "knapsack.cpp"

solver_output DPSolver::solve(solver_input input) {
    solver_output solver_output_i(input.num_task);

    for (auto i = 1; i <= MAX_WORKER_COUNT; i++) {
        solver_output solver_output_temp(input.num_task);
        int * p=calculate_values(input, i);
        int val = mulknap(input.num_task,
                          i,
                          p,
                          input.task_size,
                          solver_output_temp.solution,
                          input.worker_capacity);
        delete(p);
        if (i==1 || val > solver_output_i.max_value) {
            solver_output_i.max_value = val;
            for (auto t = 0; t < input.num_task; t++) {
                solver_output_i.solution[t] = solver_output_temp.solution[t];
            }
        }
    }
    return solver_output_i;
}

int *DPSolver::calculate_values(solver_input input, int num_bins) {
    int *p = new int[input.num_task];
    for (int i = 0; i < input.num_task; i++) {
        int size_category;
        if(input.task_size[i]>=64 && input.task_size[i]<128) size_category=1;
        if(input.task_size[i]>=128 && input.task_size[i]<256) size_category=2;
        if(input.task_size[i]>=256 && input.task_size[i]<512) size_category=3;
        if(input.task_size[i]>=512 && input.task_size[i]<1024) size_category=4;
        if(input.task_size[i]>=1024 && input.task_size[i]<2048) size_category=5;
        for (int j = 0; j < num_bins; j++) {
            int val = ((5*input.worker_score[j])/size_category - WORKER_ENERGY[j]
            );
            if (j == 0) p[i] = val;
            else if (p[i] > val) p[i] = val;
        }
    }

    return p;
}
