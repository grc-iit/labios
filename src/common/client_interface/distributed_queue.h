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

    virtual int publish_task(task *task_t){
        throw NotImplementedException("publish_task");
    }
    virtual task * subscribe_task_with_timeout( int &status){
        throw NotImplementedException("subscribe_task_with_timeout");
    }

    virtual task * subscribe_task( int &status){
        throw NotImplementedException("subscribe_task");
    }

    virtual int get_queue_size(){
        throw NotImplementedException("get_queue_size");
    }
    virtual int get_queue_count(){
        throw NotImplementedException("get_queue_count");
    }
    virtual int get_queue_count_limit(){
        throw NotImplementedException("get_queue_count");
    }
};


#endif //PORUS_MAIN_DISTRIBUTEDQUEUUE_H
