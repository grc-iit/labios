//
// Created by hdevarajan on 5/8/18.
//

#include "dp_solver.h"
#include "knapsack.cpp"

solver_output DPSolver::solve(solver_input input) {
    solver_output solver_output_i(input.num_task);
    /*for(auto t=0;t<MAX_WORKER_COUNT;t++){
        input.worker_capacity[t]=input.worker_capacity[t]/(1024*1024);
        std::cout<<"capacity:"<<input.worker_capacity[t]<<std::endl;
    }
    for(auto t = 0; t < input.num_task; t++){
        input.task_size[t]=input.task_size[t]/(1024*1024);
        std::cout<<" task_size:"<<input.task_size[t]<<std::endl;
    }*/

    for (auto i = 1; i <= MAX_WORKER_COUNT; i++) {
        solver_output solver_output_temp(input.num_task);
        int * p=calculate_values(input, i);
        solver_output_i.max_value=-1;
        int val = mulknap(input.num_task,
                          i,
                          p,
                          input.task_size,
                          solver_output_temp.solution,
                          input.worker_capacity);
        std::cout<<"solver max value:"<<val<<std::endl;
        bool all_tasks_scheduled=true;
        for (auto t = 0; t < input.num_task; t++) {
            std::cout<<"task:"<<(t)<<" worker:"<<solver_output_temp.solution[t]-1<<std::endl;
            if(solver_output_temp.solution[t]==0) {
                all_tasks_scheduled=false;
                break;
            }
        }
        if (all_tasks_scheduled && val > solver_output_i.max_value) {
            solver_output_i.max_value = val;
            for (auto t = 0; t < input.num_task; t++) {
                solver_output_i.solution[t] = solver_output_temp.solution[t];
            }
            delete(p);
            break;
        }
        delete(p);
    }
    std::cout<<"Final Solution"<<std::endl;
    for(int i=0;i<input.num_task;i++){
        std::cout<<"task:"<<(i+1)<<" worker:"<<solver_output_i.solution[i]<<std::endl;
    }
    return solver_output_i;
}

int *DPSolver::calculate_values(solver_input input, int num_bins) {
    int *p = new int[input.num_task];
    for (int i = 0; i < input.num_task; i++) {
        int size_category;
        if(input.task_size[i]>=64*1024 && input.task_size[i]<128*1024) size_category=1;
        if(input.task_size[i]>=128*1024 && input.task_size[i]<256*1024) size_category=2;
        if(input.task_size[i]>=256*1024 && input.task_size[i]<512*1024) size_category=3;
        if(input.task_size[i]>=512*1024 && input.task_size[i]<1024*1024) size_category=4;
        if(input.task_size[i]>=1024*1024 && input.task_size[i]<=2048*1024) size_category=5;
        /*if(input.task_size[i]>=1 && input.task_size[i]<=2) size_category=5;*/
        for (int j = 0; j < num_bins; j++) {
            int val = 5+input.worker_score[j] - input.worker_energy[j];
            if (j == 0) p[i] = val;
            else if (p[i] > val) p[i] = val;
        }
    }

    return p;
}
