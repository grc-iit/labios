//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_DISTRIBUTEDQUEUUE_H
#define PORUS_MAIN_DISTRIBUTEDQUEUUE_H


#include <memory>
#include "../enumerations.h"
#include "../data_structures.h"
#include <nats.h>
#include "../external_clients/serialization_manager.h"
#include "../../System.h"
class DistributedQueue {
private:
    static std::shared_ptr<DistributedQueue> instance;
    Service service;
    natsConnection      *nc  = NULL;
    natsSubscription    *sub = NULL;
    DistributedQueue(Service service):service(service){
        natsConnection_ConnectTo(&nc, NATS_URL_CLIENT.c_str());

    }
public:
    inline static std::shared_ptr<DistributedQueue> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<DistributedQueue>(new DistributedQueue(service))
                                  : instance;
    }
    int publish_task(task *task_t);
    int subscribe_task(task &task_t);
};


#endif //PORUS_MAIN_DISTRIBUTEDQUEUUE_H
