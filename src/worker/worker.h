/*******************************************************************************
* Created by hariharan on 5/10/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_WORKERSERVICE_H
#define AETRIO_MAIN_WORKERSERVICE_H
/******************************************************************************
*include files
******************************************************************************/
#include "../task_scheduler/task_scheduler.h"
#include "api/io_client.h"
#include "../common/external_clients/memcached_impl.h"
#include "api/posix_client.h"
#include "../aetrio_system.h"
#include "api/posix_client.h"
/******************************************************************************
*Class
******************************************************************************/
class worker {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
    static std::shared_ptr<worker> instance;
    service service_i;
    int worker_index;
    std::shared_ptr<distributed_queue> queue;
    std::shared_ptr<distributed_hashmap> map;
    std::shared_ptr<io_client> client;
/******************************************************************************
*Constructor
******************************************************************************/
    worker(service service,int worker_index)
            :service_i(service),kill(false),worker_index(worker_index){
        if(io_client_type_t==io_client_type::POSIX){
            client=std::make_shared<posix_client>(posix_client(worker_index));
        }
        queue=aetrio_system::getInstance(service_i)->get_worker_queue(worker_index);
        map=aetrio_system::getInstance(service_i)->map_server;
    }
/******************************************************************************
*Interface
******************************************************************************/
    int setup_working_dir();
    int calculate_worker_score(bool before_sleeping);
    int update_capacity();
    int64_t get_total_capacity();
    int64_t get_current_capacity();
    float get_remaining_capacity();
    int update_score(bool before_sleeping);
public:
    int run();
    int kill;
    inline static std::shared_ptr<worker>
            getInstance(service service,int worker_index){
        return instance== nullptr ? instance=std::shared_ptr<worker>
                (new worker(service,worker_index)) : instance;}
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~worker(){}
};

#endif //AETRIO_MAIN_WORKERSERVICE_H
