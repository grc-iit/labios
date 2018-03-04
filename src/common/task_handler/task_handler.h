//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_TASK_HANDLER_H
#define PORUS_MAIN_TASK_HANDLER_H


#include <memory>
#include "../enumeration.h"
#include "../external_clients/DistributedQueue.h"
#include "../structure.h"

class task_handler {
private:
    static std::shared_ptr<task_handler> instance;
    Service service;
    task_handler(Service service):service(service){}
public:
    inline static std::shared_ptr<task_handler> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<task_handler>(new task_handler(service))
                                  : instance;
    }
    int submit(task *task_t);
    std::vector<write_task> build_task_write(write_task task);
    std::vector<read_task> build_task_read(read_task task);
};


#endif //PORUS_MAIN_TASK_HANDLER_H
