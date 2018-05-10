//
// Created by hdevarajan on 5/7/18.
//

#ifndef PORUS_MAIN_NATSCLIENT_H
#define PORUS_MAIN_NATSCLIENT_H


#include "../client_interface/DistributedQueue.h"


class NatsImpl: public DistributedQueue {
private:
    natsConnection      *nc  = NULL;
    natsSubscription    *sub = NULL;

public:
    NatsImpl(Service service,std::string url): DistributedQueue(service) {
        natsConnection_ConnectTo(&nc, url.c_str());
    }
    int publish_task(task *task_t, std::string subject) override;
    int subscribe_task(task &task_t, std::string subject) override;

};


#endif //PORUS_MAIN_NATSCLIENT_H
