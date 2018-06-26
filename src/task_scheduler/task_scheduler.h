/*******************************************************************************
* Created by hariharan on 5/9/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_TASK_SCHEDULER_SERVICE_H
#define AETRIO_MAIN_TASK_SCHEDULER_SERVICE_H
/******************************************************************************
*include files
******************************************************************************/
#include <memory>
#include "../common/enumerations.h"
#include <zconf.h>
#include "../common/external_clients/nats_impl.h"
#include "../common/timer.h"
/******************************************************************************
*Class
******************************************************************************/
class task_scheduler {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
    static std::shared_ptr<task_scheduler> instance;
    service service_i;
/******************************************************************************
*Constructor
******************************************************************************/
    explicit task_scheduler(service service):service_i(service),kill(false){}
/******************************************************************************
*Interface
******************************************************************************/
    void schedule_tasks(std::vector<task *> &tasks, int write_count,
                        int read_count);
public:
    int kill;
    inline static std::shared_ptr<task_scheduler> getInstance(service service){
        return instance== nullptr ? instance=
                std::shared_ptr<task_scheduler>
                        (new task_scheduler(service)) : instance;
    }
    int run();
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~task_scheduler(){}

};


#endif //AETRIO_MAIN_TASK_SCHEDULER_SERVICE_H
