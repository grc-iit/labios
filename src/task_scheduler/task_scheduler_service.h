//
// Created by hdevarajan on 5/9/18.
//

#ifndef PORUS_MAIN_TASK_SCHEDULER_SERVICE_H
#define PORUS_MAIN_TASK_SCHEDULER_SERVICE_H


#include <memory>
#include "../common/enumerations.h"
#include <zconf.h>
#include "../common/external_clients/NatsImpl.h"
#include "../common/Timer.h"

class task_scheduler_service {
private:
    static std::shared_ptr<task_scheduler_service> instance;
    Service service;
    task_scheduler_service(Service service):service(service),kill(false){}
    void schedule_tasks(std::vector<task> tasks,int write_count,int read_count);
public:
    int kill;
    inline static std::shared_ptr<task_scheduler_service> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<task_scheduler_service>(new task_scheduler_service(service))
                                  : instance;
    }
    int run();

};


#endif //PORUS_MAIN_TASK_SCHEDULER_SERVICE_H
