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
#include "../exceptions.h"

class distributed_queue {
private:

protected:
    service service_i;
    distributed_queue(service service):service_i(service){


    }
public:

    virtual int publish_task(task *task_t, std::string subject){
        throw NotImplementedException("publish_task");
    }
    virtual int subscribe_task(task &task_t, std::string subject){
        throw NotImplementedException("subscribe_task");
    }
};


#endif //PORUS_MAIN_DISTRIBUTEDQUEUUE_H
