


#include "../common/data_structures.h"
#include "solver/DPSolver.h"

int main() {
    int tasks = 10;

    int p_arr[10] = {1,1,1,1,1,1,1,1,1,1};
    int x[4];
    int workers = 4;
    int worker_busyness[4] = {25, 40, 15, 100};
    int worker_capacity[4] = {2, 5, 5, 5};
    solver_input input(tasks,workers);
    for(auto i=0;i<tasks;i++){
        input.task_size[i]=1+i/5;
    }
    for(auto i=0;i<tasks;i++){
        input.task_size[i]=1+i/5;
    }
    for(auto i=0;i<workers;i++){
        input.worker_score[i]=1+i/5;
    }
    for(auto i=0;i<workers;i++){
        input.worker_capacity[i]=i+2;
    }
    for(auto i=0;i<tasks;i++){
        input.task_value[i]=1;
    }
    input.num_task = tasks;
    DPSolver solver=DPSolver();
    solver_output output=solver.solve(input);
    std::cout<<output.max_value<< std::endl;
    for(int i=0;i<tasks;i++){
        std::cout<<*(output.solution+i)<< std::endl;;
    }
    return 0;
}