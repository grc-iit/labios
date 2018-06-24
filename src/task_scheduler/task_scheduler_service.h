//
// Created by hdevarajan on 5/9/18.
//

#ifndef AETRIO_MAIN_TASK_SCHEDULER_SERVICE_H
#define AETRIO_MAIN_TASK_SCHEDULER_SERVICE_H


#include <memory>
#include "../common/enumerations.h"
#include <zconf.h>
#include "../common/external_clients/nats_impl.h"
#include "../common/timer.h"

class task_scheduler_service {
private:
    static std::shared_ptr<task_scheduler_service> instance;
    service service_i;
    explicit task_scheduler_service(service service):service_i(service),kill(false){}
    void schedule_tasks(std::vector<task *> &tasks, int write_count,
                        int read_count);
public:
    int kill;
    inline static std::shared_ptr<task_scheduler_service> getInstance(service service){
        return instance== nullptr ? instance=
                std::shared_ptr<task_scheduler_service>
                        (new task_scheduler_service(service)) : instance;
    }
    int run();

};


#endif //AETRIO_MAIN_TASK_SCHEDULER_SERVICE_H
