/******************************************************************************
*include files
******************************************************************************/
#include <algorithm>
#include <iomanip>
#include "task_scheduler.h"
#include "../common/external_clients/memcached_impl.h"
#include "../aetrio_system.h"
#include "../common/data_structures.h"

std::shared_ptr<task_scheduler> task_scheduler::instance = nullptr;
service task_scheduler::service_i = service();
/******************************************************************************
*Interface
******************************************************************************/
int task_scheduler::run() {
    auto queue = aetrio_system::getInstance(service_i)
            ->get_client_queue(CLIENT_TASK_SUBJECT);
    auto task_list = std::vector<task*>();
    Timer t=Timer();
    t.startTime();
    int status;

    while(!kill){
        status=-1;
        auto task_i= queue->subscribe_task_with_timeout(status);
        if(status!=-1 && task_i!= nullptr){
            task_list.push_back(task_i);
        }
        auto time_elapsed = t.stopTime();
        if(!task_list.empty() &&
           (task_list.size()>=MAX_NUM_TASKS_IN_QUEUE
            ||time_elapsed>=MAX_SCHEDULE_TIMER)){
            scheduling_threads.submit(std::bind(schedule_tasks, task_list));
            //schedule_tasks(task_list);
            t.startTime();
            task_list.clear();
        }
    }
    return 0;
}


void task_scheduler::schedule_tasks(std::vector<task*> &tasks) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    auto solver_i=aetrio_system::getInstance(service_i)->solver_i;
    solver_input input(tasks, static_cast<int>(tasks.size()));
    solver_output output=solver_i->solve(input);

    for (auto element : output.worker_task_map){
        auto queue=aetrio_system::getInstance(service_i)->
                get_worker_queue(element.first);
        for(auto task:element.second){
#ifdef DEBUG
            std::cout << "threadID:" <<std::this_thread::get_id()
                              << "\tTask#" << task->task_id
                              << "\tWorker#" << element.first
                              << "\n";
#endif
            switch (task->t_type){
                case task_type::WRITE_TASK:{
                    auto *wt= reinterpret_cast<write_task *>(task);
                    queue->publish_task(wt);
                    break;
                }
                case task_type::READ_TASK:{
                    auto *rt= reinterpret_cast<read_task *>(task);
                    queue->publish_task(rt);
                    break;
                }
            }
        }
    }
    for(auto task:tasks){
        delete task;
    }
    delete input.task_size;
    delete output.solution;
#ifdef TIMER
    std::cout << "task_scheduler::schedule_tasks(),"
              <<std::fixed<<std::setprecision(10)
              <<t.pauseTime()<<"\n";
#endif
}
