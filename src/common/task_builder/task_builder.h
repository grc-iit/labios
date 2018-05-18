//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_TASK_HANDLER_H
#define PORUS_MAIN_TASK_HANDLER_H


#include <memory>
#include "../enumerations.h"
#include "../client_interface/distributed_queue.h"
#include "../data_structures.h"
#include <chrono>
using namespace std::chrono;
class task_builder {
private:
    static std::shared_ptr<task_builder> instance;
    service service_i;
    task_builder(service service):service_i(service){}
public:
    inline static std::shared_ptr<task_builder> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<task_builder>(new task_builder(service))
                                  : instance;
    }
    std::vector<write_task> build_task_write(write_task task,std::string data);
    std::vector<read_task> build_task_read(read_task task);
};


#endif //PORUS_MAIN_TASK_HANDLER_H
