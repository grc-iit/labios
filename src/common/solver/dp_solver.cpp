/******************************************************************************
*include files
******************************************************************************/
#include <algorithm>
#include "dp_solver.h"
#include "knapsack.cpp"
#include "../../aetrio_system.h"

/******************************************************************************
*Interface
******************************************************************************/
solver_output DPSolver::solve(solver_input input) {

    int write_index=0,read_index=0,actual_index=0;
    auto solver_task_map=std::unordered_map<int,int>();
    auto static_task_map=std::unordered_map<int,int>();

    for(auto task_t:input.tasks){
        switch (task_t->t_type){
            case task_type::WRITE_TASK:{
                auto *wt= reinterpret_cast<write_task *>(task_t);
                if(wt->destination.worker==-1){
                    solver_task_map.emplace(actual_index,write_index);
                    write_index++;
                }else{
                    /*
                     * update existing file
                     */
                    static_task_map.emplace(actual_index,wt->destination.worker-1);
                }
                break;
            }
            case task_type::READ_TASK:{
                auto *rt= reinterpret_cast<read_task *>(task_t);
                static_task_map.emplace(actual_index,rt->source.worker-1);
                read_index++;
                break;
            }
            default:
                std::cout <<"schedule_tasks(): Error in task type\n";
                break;
        }
        actual_index++;
    }
    auto map=aetrio_system::getInstance(service_i)->map_server;
    auto sorted_workers=std::vector<std::pair<int,int>>();
    int original_index=0;
    for(int worker_index=0;worker_index<MAX_WORKER_COUNT;worker_index++){
        std::string val=map->get(table::WORKER_CAPACITY,std::to_string(worker_index+1));
        sorted_workers.emplace_back(std::make_pair(atoi(val.c_str()),
                                             original_index++));
    }
    std::sort(sorted_workers.begin(), sorted_workers.end());
    int new_index=0;
    for(auto pair:sorted_workers){
        std::string val=map->get(table::WORKER_SCORE,std::to_string(pair.second+1));
        worker_score[new_index]=atoi(val.c_str());
        worker_capacity[new_index]=pair.first;
        worker_energy[new_index]=WORKER_ENERGY[pair.second];
//        std::cout<<"worker:"<<pair.second+1<<" capacity:"<<worker_capacity[new_index]<<" score:"<<worker_score[new_index]<<std::endl;
        new_index++;
    }
    solver_output solver_output_i(input.num_tasks);
    int max_value=0;

    for (auto i = 1; i <= MAX_WORKER_COUNT; i++) {
        solver_output solver_output_temp(input.num_tasks);
        int * p=calculate_values(input, i);
        max_value=-1;
        int val = mulknap(input.num_tasks,
                          i,
                          p,
                          input.task_size,
                          solver_output_temp.solution,
                          worker_capacity);
        
        bool all_tasks_scheduled=true;
        for (auto t = 0; t < input.num_tasks; t++) {
            std::cout<<"task:"<<(t)<<" worker:"<<solver_output_temp.solution[t]-1<<std::endl;
            if(solver_output_temp.solution[t]==0) {
                all_tasks_scheduled=false;
                break;
            }
        }
        if (all_tasks_scheduled && val > max_value) {
            max_value = val;
            for (auto t = 0; t < input.num_tasks; t++) {
                solver_output_i.solution[t] = solver_output_temp.solution[t];
            }
            delete(p);
            break;
        }
        delete(p);
    }
    //check if there is a solution for the DPSolver
    for(int t=0;t<input.num_tasks;t++){
        if(solver_output_i.solution[t]-1 < 0 || solver_output_i.solution[t]-1 > MAX_WORKER_COUNT){
            throw std::runtime_error("DPSolver::solve(): No Solution found\n");
        }
        solver_output_i.solution[t]=sorted_workers[solver_output_i.solution[t]-1].second;
    }
//    std::cout<<"Final Solution"<<std::endl;
//    for(int i=0;i<input.num_tasks;i++){
//        std::cout<<"task:"<<(i+1)<<" worker:"<<solver_output_i.solution[i]<<std::endl;
//    }

    /**
     * merge all tasks (read or write) in order based on what solver gave us
     * or static info we already had
     */
    for(int task_index=0;task_index<input.tasks.size();task_index++){
        auto read_iter=static_task_map.find(task_index);
        int worker_index=-1;
        if(read_iter==static_task_map.end()){
            auto write_iter=solver_task_map.find(task_index);
            if(write_iter==solver_task_map.end()){
                printf("Error");
            }else{
                write_index=write_iter->second;
                worker_index=solver_output_i.solution[write_index];
            }
        }else{
            worker_index=read_iter->second;
        }
        auto worker_task_iter=solver_output_i.worker_task_map.find(worker_index);
        std::vector<task*> worker_tasks;
        if(worker_task_iter==solver_output_i.worker_task_map.end()){
            worker_tasks=std::vector<task*>();
        }else{
            worker_tasks=worker_task_iter->second;
            solver_output_i.worker_task_map.erase(worker_task_iter);
        }
        task* task_i=input.tasks[task_index];
        switch (task_i->t_type){
            case task_type::WRITE_TASK:{
                auto *wt= reinterpret_cast<write_task *>(input.tasks[task_index]);
                worker_tasks.push_back(wt);
                break;
            }
            case task_type::READ_TASK:{
                auto *rt= reinterpret_cast<read_task *>(input.tasks[task_index]);
                worker_tasks.push_back(rt);
                break;
            }
        }

        solver_output_i.worker_task_map.emplace(worker_index,worker_tasks);
    }
    return solver_output_i;
}

int *DPSolver::calculate_values(solver_input input, int num_bins) {
    int *p = new int[input.num_tasks];
    for (int i = 0; i < input.num_tasks; i++) {
        int size_category;
        if(input.task_size[i]>=64*1024 && input.task_size[i]<128*1024) size_category=1;
        if(input.task_size[i]>=128*1024 && input.task_size[i]<256*1024) size_category=2;
        if(input.task_size[i]>=256*1024 && input.task_size[i]<512*1024) size_category=3;
        if(input.task_size[i]>=512*1024 && input.task_size[i]<1024*1024) size_category=4;
        if(input.task_size[i]>=1024*1024 && input.task_size[i]<=2048*1024) size_category=5;
        /*if(input.task_size[i]>=1 && input.task_size[i]<=2) size_category=5;*/
        for (int j = 0; j < num_bins; j++) {
            int val = 5+worker_score[j] - worker_energy[j];
            if (j == 0) p[i] = val;
            else if (p[i] > val) p[i] = val;
        }
    }
    return p;
}
