//
// Created by hdevarajan on 5/7/18.
//

#include "nats_impl.h"
#include "../../aetrio_system.h"

int NatsImpl::publish_task(task *task_t) {
    std::string msg;
    serialization_manager sm=serialization_manager();
    msg=sm.serialise_task(task_t);
    std::shared_ptr<aetrio_system> sys=aetrio_system::getInstance(service_i);
    natsConnection_PublishString(nc, subject.c_str(), msg.c_str());
    return 0;
}

task*  NatsImpl::subscribe_task_with_timeout(int &status) {
    std::shared_ptr<aetrio_system> sys=aetrio_system::getInstance(service_i);
    serialization_manager sm=serialization_manager();
    natsMsg *msg = NULL;
    natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER_MS);
    if(msg==NULL) return nullptr;
    task* t=sm.deserialise_task(natsMsg_GetData(msg));
    status=0;
    return t;
}

task* NatsImpl::subscribe_task(int &status) {
    std::shared_ptr<aetrio_system> sys=aetrio_system::getInstance(service_i);
    serialization_manager sm=serialization_manager();
    natsMsg *msg = NULL;
    natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER_MS_MAX);
    if(msg==NULL) return nullptr;
    task* t=sm.deserialise_task(natsMsg_GetData(msg));
    status=0;
    return t;
}

int NatsImpl::get_queue_size() {
    int size_of_queue;
    natsSubscription_GetPending(sub,NULL,&size_of_queue);
    return size_of_queue;
}

int NatsImpl::get_queue_count() {
    int* count_of_queue=new int();
    natsSubscription_GetStats(sub,count_of_queue,NULL,NULL,NULL,NULL,NULL);
    int queue_count=*count_of_queue;
    delete(count_of_queue);
    return queue_count;
}

int NatsImpl::get_queue_count_limit() {
    return INT_MAX;
}