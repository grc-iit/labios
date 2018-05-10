//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_TASK_HANDLER_H
#define PORUS_MAIN_TASK_HANDLER_H


#include <memory>
#include "../enumerations.h"
#include "../client_interface/DistributedQueue.h"
#include "../data_structures.h"

class task_handler {
private:
    static std::shared_ptr<task_handler> instance;
    Service service;
    std::string subject;
    std::shared_ptr<DistributedQueue> dq;
    task_handler(Service service,
            std::shared_ptr<DistributedQueue> dq,
            std::string subject):service(service),dq(dq),subject(subject){}
public:
    inline static std::shared_ptr<task_handler> getInstance(Service service,
                                                            std::shared_ptr<DistributedQueue> dq,
                                                            std::string subject){
        return instance== nullptr ? instance=std::shared_ptr<task_handler>(new task_handler(service,dq,subject))
                                  : instance;
    }
    int submit(task *task_t);
    std::vector<write_task> build_task_write(write_task task);
    std::vector<read_task> build_task_read(read_task task);
};


#endif //PORUS_MAIN_TASK_HANDLER_H
