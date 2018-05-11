//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_TASK_HANDLER_H
#define PORUS_MAIN_TASK_HANDLER_H


#include <memory>
#include "../enumerations.h"
#include "../client_interface/distributed_queue.h"
#include "../data_structures.h"

class task_handler {
private:
    static std::shared_ptr<task_handler> instance;
    service service_i;
    std::shared_ptr<distributed_queue> dq;
    task_handler(service service,
            std::shared_ptr<distributed_queue> dq):service_i(service),dq(dq){}
public:
    inline static std::shared_ptr<task_handler> getInstance(service service,
                                                            std::shared_ptr<distributed_queue> dq){
        return instance== nullptr ? instance=std::shared_ptr<task_handler>(new task_handler(service,dq))
                                  : instance;
    }
    int submit(task *task_t);
    std::vector<write_task> build_task_write(write_task task,std::string data);
    std::vector<read_task> build_task_read(read_task task);
};


#endif //PORUS_MAIN_TASK_HANDLER_H
