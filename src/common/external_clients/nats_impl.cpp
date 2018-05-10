//
// Created by hdevarajan on 5/7/18.
//

#include "nats_impl.h"
#include "../../system.h"

int NatsImpl::publish_task(task *task_t, std::string subject) {
    std::string msg;
    serialization_manager sm=serialization_manager();
    msg=sm.serialise_task(task_t);
    std::shared_ptr<System> sys=System::getInstance(service);
    natsConnection_PublishString(nc, subject.c_str(), msg.c_str());
    return 0;
}

int NatsImpl::subscribe_task(task &task_t, std::string subject) {
    std::shared_ptr<System> sys=System::getInstance(service);
    serialization_manager sm=serialization_manager();
    natsConnection_SubscribeSync(&sub, nc, subject.c_str());
    natsMsg *msg = NULL;
    natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER);
    if(msg==NULL) return -1;
    task_t=sm.deserialise_task(natsMsg_GetData(msg));
    return 0;
}
