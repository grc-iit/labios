/******************************************************************************
*include files
******************************************************************************/
#include <algorithm>
#include "task_scheduler.h"
#include "../common/external_clients/memcached_impl.h"
#include "../aetrio_system.h"
std::shared_ptr<task_scheduler> task_scheduler::instance = nullptr;
service task_scheduler::service_i = service();
/******************************************************************************
*Interface
******************************************************************************/
int task_scheduler::run() {

    int count=0, static_count=0, solve_count=0;
    auto queue = aetrio_system::getInstance(service_i)
                    ->get_queue_client(CLIENT_TASK_SUBJECT);
    auto task_list = std::vector<task*>();
    Timer t=Timer();
    t.startTime();
    int status;

    while(!kill){
        status=-1;
        auto task_i= queue->subscribe_task_with_timeout(status);
        if(status!=-1 && task_i!= nullptr){
            count++;
            switch (task_i->t_type){
                case task_type::WRITE_TASK:{
                    auto *wt= reinterpret_cast<write_task *>(task_i);
                    //std::cout<<serialization_manager().serialize_task(wt)<<"\n";
                    task_list.push_back(wt);
                    solve_count++;
                    break;
                }
                case task_type::READ_TASK:{
                    auto *rt= reinterpret_cast<read_task *>(task_i);
                    //std::cout<<serialization_manager().serialize_task(rt)<<"\n";
                    task_list.push_back(rt);
                    static_count++;
                    break;
                }
                case task_type::DELETE_TASK:{
                    auto *dt= reinterpret_cast<delete_task*>(task_i);
                    //std::cout<<serialization_manager().serialize_task(dt)<<"\n";
                    task_list.push_back(dt);
                    static_count++;
                    break;
                }
                case task_type::FLUSH_TASK:{
                    auto *ft= reinterpret_cast<flush_task*>(task_i);
                    //std::cout<<serialization_manager().serialize_task(ft)<<"\n";
                    task_list.push_back(ft);
                    static_count++;
                    break;
                }
                default:
                    std::cerr <<"run(): Error in task type\n";
                    break;
            }
        }
        auto time_elapsed= t.stopTime();
        std::cout << "num_tasks:\t" << task_list.size()<<"\n";
        if(!task_list.empty() &&
           (count>MAX_NUM_TASKS_IN_QUEUE ||time_elapsed>MAX_SCHEDULE_TIMER)){
            workers.submit(std::bind(schedule_tasks, task_list));
            count=0;
            t.startTime();
            task_list.clear();
        }
    }
    return 0;
}
/**
 * listener_thread-> scheduler_thread->1solver:2solver:3solver
                                  ->1sender
 */

void task_scheduler::schedule_tasks(std::vector<task*> &tasks) {
    /**
     * shuffle tasks to identify which ones need solving and which ones are
     * static
     * call solver to get a map of worker to list of tasks
     * merge static with the solver solution
     * publish to individual worker queue
     *
     */
    //std::cout << "THREAD ID:\t" <<std::this_thread::get_id() << std::endl;

    solver_input input(tasks);
//    std::cout << "tasks.size():\t" << input.tasks.size()<<"\n";
//    std::cout << "num_tasks:\t" << input.num_tasks<<"\n";
//    for(int i=0;i<input.num_tasks;++i){
//        std::cout << "task "<<i<<" size:\t" << input.task_size[i]<<"\n";
//    }
//    std::cout << "Total I/O:\t" << input.total_io_size<<"\n";


    auto solver_i=aetrio_system::getInstance(service_i)->solver_i;

    solver_output output=solver_i->solve(input);


    for (std::pair<int,std::vector<task*>> element : output.worker_task_map)
    {
        std::cout<<"add to worker:"<<element.first+1<<std::endl;
        std::shared_ptr<distributed_queue> queue=aetrio_system::getInstance(service_i)->get_worker_queue(element.first+1);
        for(auto task:element.second){
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
}
