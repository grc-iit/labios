//
// Created by hdevarajan on 5/9/18.
//

#include "task_scheduler_service.h"
#include "../common/external_clients/memcached_impl.h"
#include "../porus_system.h"

std::shared_ptr<task_scheduler_service> task_scheduler_service::instance = nullptr;

int task_scheduler_service::run() {
    int count=0, read_count=0, write_count=0;

    std::shared_ptr<distributed_queue> queue=porus_system::getInstance(service_i)->queue_client;
    std::vector<task> task_list=std::vector<task>();
    Timer t=Timer();
    t.startTime();
    while(!kill){
        usleep(10);
        task task_i(task_type::WRITE_TASK);
        int status=queue->subscribe_task(task_i,CLIENT_TASK_SUBJECT);
        if(status!=-1){
            count++;
            task_list.push_back(task_i);
            switch (task_i.t_type){
                case WRITE_TASK:{
                    write_count++;
                    break;
                }
                case READ_TASK:{
                    read_count++;
                    break;
                }
            }
        }
        auto time_elapsed= t.endTimeWithoutPrint("");
        if(task_list.size() >0 && (count>MAX_TASK || time_elapsed>MAX_TASK_TIMER)){
            schedule_tasks(task_list,write_count,read_count);
            count=0;
            t.startTime();
            task_list.empty();
        }
    }
    return 0;
}

void task_scheduler_service::schedule_tasks(std::vector<task> tasks,int write_count,int read_count) {
    solver_input input(write_count,MAX_WORKER_COUNT);
    input.num_task=write_count;
    int write_index=0,read_index=0,actual_index=0;
    std::unordered_map<int,std::vector<task>> worker_tasks_map=std::unordered_map<int,std::vector<task>>();
    std::unordered_map<int,int> write_task_solver=std::unordered_map<int,int>();
    std::unordered_map<int,int> read_task_solver=std::unordered_map<int,int>();
    int i=0;
    for(auto task_t:tasks){

        switch (task_t.t_type){
            case WRITE_TASK:{
                write_task *wt= static_cast<write_task *>(&task_t);
                input.task_size[write_index]=wt->source.size;
                write_task_solver.emplace(actual_index,write_index);
                write_index++;
                break;
            }
            case READ_TASK:{
                int worker_index=0;
                read_task *rt= static_cast<read_task *>(&task_t);
                //TODO:calculate which worker has data from MDM
                read_task_solver.emplace(actual_index,worker_index);
                read_index++;
                break;
            }
        }
        actual_index++;
    }
    std::shared_ptr<distributed_hashmap> map=porus_system::getInstance(service_i)->map_server;
    for(int worker_index=0;worker_index<MAX_WORKER_COUNT;worker_index++){
        std::string val=map->get(table::WORKER_SCORE,std::to_string(worker_index));
        input.worker_score[worker_index]=atoi(val.c_str());
        val=map->get(table::WORKER_CAPACITY,std::to_string(worker_index));
        input.worker_capacity[worker_index]=atoi(val.c_str());
    }
    std::shared_ptr<solver> solver_i=porus_system::getInstance(service_i)->solver_i;
    solver_output output=solver_i->solve(input);
    for(int task_index=0;task_index<tasks.size();task_index++){
        auto read_iter=read_task_solver.find(task_index);
        int worker_index=-1;
        if(read_iter==read_task_solver.end()){
            auto write_iter=write_task_solver.find(task_index);
            if(write_iter==write_task_solver.end()){
                printf("Error");
            }else{
                int write_index=write_iter->second;
                worker_index=output.solution[write_index];
            }
        }else{
            worker_index=read_iter->second;
        }
        auto worker_task_iter=worker_tasks_map.find(worker_index);
        std::vector<task> worker_tasks;
        if(worker_task_iter==worker_tasks_map.end()){
            worker_tasks=std::vector<task>();
        }else{
            worker_tasks=worker_task_iter->second;
            worker_tasks_map.erase(worker_task_iter);
        }
        task task_i=tasks[task_index];
        switch (task_i.t_type){
            case WRITE_TASK:{
                write_task *wt= static_cast<write_task *>(&tasks[task_index]);
                worker_tasks.push_back(*wt);
                break;
            }
            case READ_TASK:{
                read_task *rt= static_cast<read_task *>(&tasks[task_index]);
                worker_tasks.push_back(*rt);
                break;
            }
        }

        worker_tasks_map.emplace(worker_index,worker_tasks);
    }
    for (std::pair<int,std::vector<task>> element : worker_tasks_map)
    {
        std::shared_ptr<distributed_queue> queue=porus_system::getInstance(service_i)->worker_queue[element.first];
        for(auto task:element.second){
            queue->publish_task(&task,WORKER_TASK_SUBJECT[element.first]);
        }
    }
}
