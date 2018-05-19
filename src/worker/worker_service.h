//
// Created by hdevarajan on 5/10/18.
//

#ifndef PORUS_MAIN_WORKERSERVICE_H
#define PORUS_MAIN_WORKERSERVICE_H


#include "../task_scheduler/task_scheduler_service.h"
#include "api/io_client.h"
#include "../common/external_clients/memcached_impl.h"
#include "api/posix_client.h"
#include "../aetrio_system.h"
#include "api/posix_client.h"

class worker_service {
private:
    static std::shared_ptr<worker_service> instance;
    service service_i;
    int worker_index;
    std::shared_ptr<distributed_queue> queue;
    std::shared_ptr<distributed_hashmap> map;
    std::shared_ptr<io_client> client;
    worker_service(service service,int worker_index):service_i(service),kill(false),worker_index(worker_index){
        if(io_client_type_t==io_client_type::POSIX){
            client= std::shared_ptr<posix_client>(new posix_client(worker_index));
        }
        queue=aetrio_system::getInstance(service_i)->get_worker_queue(worker_index,WORKER_TASK_SUBJECT[worker_index]);
        map=aetrio_system::getInstance(service_i)->map_server;

    }

    int calculate_worker_score();
    int update_capacity();
    int update_score();
public:
    int kill;
    inline static std::shared_ptr<worker_service> getInstance(service service,int worker_index){
        return instance== nullptr ? instance=std::shared_ptr<worker_service>(new worker_service(service,worker_index))
                                  : instance;
    }
    int run();
};


#endif //PORUS_MAIN_WORKERSERVICE_H
