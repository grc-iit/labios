//
// Created by hdevarajan on 5/7/18.
//

#ifndef AETRIO_MAIN_NATSCLIENT_H
#define AETRIO_MAIN_NATSCLIENT_H


#include "../client_interface/distributed_queue.h"


class NatsImpl: public distributed_queue {
private:
    natsConnection      *nc  = nullptr;
    natsSubscription    *sub = nullptr;
    std::string subject;
public:
    NatsImpl(service service,std::string url,std::string subject):
            distributed_queue(service),subject(subject) {
        natsConnection_ConnectTo(&nc, url.c_str());
        natsConnection_SubscribeSync(&sub, nc, subject.c_str());
    }
    int publish_task(task *task_t) override;
    task * subscribe_task_with_timeout( int &status) override;
    task * subscribe_task(int &status) override;

    int get_queue_count() override;
    int get_queue_size() override;
    int get_queue_count_limit() override;
};


#endif //AETRIO_MAIN_NATSCLIENT_H
