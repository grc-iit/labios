//
// Created by hdevarajan on 5/7/18.
//

#include "NatsImpl.h"
#include "../../System.h"

int NatsImpl::publish_task(task *task_t) {
    std::string msg;
    serialization_manager sm=serialization_manager();
    msg=sm.serialise_task(task_t);
    std::shared_ptr<System> sys=System::getInstance(service);
    std::string subject=sys->get_task_subject();
    natsConnection_PublishString(nc, subject.c_str(), msg.c_str());
    return 0;
}

int NatsImpl::subscribe_task(task &task_t) {
    std::shared_ptr<System> sys=System::getInstance(service);
    serialization_manager sm=serialization_manager();
    std::string subject=sys->get_task_subject();
    natsConnection_SubscribeSync(&sub, nc, subject.c_str());
    natsMsg             *msg = NULL;
    natsSubscription_NextMsg(&msg, sub, 1000);
    task_t=sm.deserialise_task(natsMsg_GetData(msg));
    return 0;
}
