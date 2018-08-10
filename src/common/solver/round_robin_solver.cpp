/******************************************************************************
*include files
******************************************************************************/
#include "round_robin_solver.h"
#include "../../aetrio_system.h"

std::shared_ptr<round_robin_solver> round_robin_solver::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
solver_output round_robin_solver::solve(solver_input input) {
    std::vector<task*> worker_tasks;
    auto map_server=aetrio_system::getInstance(service_i)->map_server();

    solver_output solution(input.num_tasks);
    for(auto i=0;i<input.tasks.size();i++){
        std::size_t worker_id=map_server->counter_inc(COUNTER_DB,ROUND_ROBIN_INDEX,
                                                      std::to_string(-1));
        worker_id = worker_id % MAX_WORKER_COUNT;
        switch(input.tasks[i]->t_type) {
            case task_type::WRITE_TASK: {
                auto *wt = reinterpret_cast<write_task *>(input.tasks[i]);
                if(wt->destination.worker==-1)
                    solution.solution[i] = static_cast<int>(worker_id+1);
                else solution.solution[i] = wt->destination.worker;
                break;
            }
            case task_type::READ_TASK: {
                auto *rt = reinterpret_cast<read_task *>(input.tasks[i]);
                if(rt->source.worker==-1)
                    solution.solution[i] = static_cast<int>(worker_id+1);
                else solution.solution[i] = rt->source.worker;
                break;
            }
            case task_type::DELETE_TASK:{
                auto *dt= reinterpret_cast<delete_task*>(input.tasks[i]);
                if(dt->source.worker==-1)
                    std::cerr<<"round_robin_solver::solve():\t "
                               "delete task failed\n";
                else solution.solution[i] = dt->source.worker;
                break;
            }
            case task_type::FLUSH_TASK:{
                auto *ft= reinterpret_cast<flush_task*>(input.tasks[i]);
                if(ft->source.worker==-1)
                    std::cerr<<"round_robin_solver::solve():\t "
                               "flush task failed\n";
                else solution.solution[i] = ft->source.worker;
                break;
            }
            default:
                std::cerr<<"round_robin_solver::solve()\t "
                           "task type invalid\n";
        }
#ifdef DEBUG
        std::cout << "Task#"<<i <<" Worker#"<<solution.solution[i]<<"\n";
#endif
        auto it=solution.worker_task_map.find(solution.solution[i]);
        if(it==solution.worker_task_map.end()){
            worker_tasks = std::vector<task*>();
            worker_tasks.push_back(input.tasks[i]);
            solution.worker_task_map.emplace
                    (std::make_pair(solution.solution[i],worker_tasks));
        }else it->second.push_back(input.tasks[i]);
    }
    return solution;
}