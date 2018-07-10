/*******************************************************************************
* Created by hariharan on 2/23/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_TASK_HANDLER_H
#define AETRIO_MAIN_TASK_HANDLER_H
/******************************************************************************
*include files
******************************************************************************/
#include <memory>
#include "../enumerations.h"
#include "../client_interface/distributed_queue.h"
#include "../data_structures.h"
#include <chrono>
/******************************************************************************
*Class
******************************************************************************/
class task_builder {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
    static std::shared_ptr<task_builder> instance;
    service service_i;
/******************************************************************************
*Constructor
******************************************************************************/
    explicit task_builder(service service):service_i(service){}
public:
/******************************************************************************
*Interface
******************************************************************************/
    inline static std::shared_ptr<task_builder> getInstance(service service){
        return instance== nullptr ? instance=std::make_shared<task_builder>
                (task_builder(service)) : instance;
    }
    std::vector<write_task*> build_write_task(write_task task, std::string
    data);
    std::vector<read_task> build_read_task(read_task task);
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~task_builder(){}
};


#endif //AETRIO_MAIN_TASK_HANDLER_H
