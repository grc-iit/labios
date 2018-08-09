/*******************************************************************************
* Created by hariharan on 5/7/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_NATSCLIENT_H
#define AETRIO_MAIN_NATSCLIENT_H
/******************************************************************************
*include files
******************************************************************************/
#include "../client_interface/distributed_queue.h"
/******************************************************************************
*Class
******************************************************************************/
class NatsImpl: public distributed_queue {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
    natsConnection      *nc  = nullptr;
    natsSubscription    *sub = nullptr;
    std::string subject;
public:
/******************************************************************************
*Constructor
******************************************************************************/
    NatsImpl(service service, const std::string &url, const std::string
    &subject,std::string queue_group)
            :distributed_queue(service),subject(subject) {
        natsConnection_ConnectTo(&nc, url.c_str());
        if(queue_group.empty()) natsConnection_SubscribeSync(&sub, nc, subject.c_str());
        else
        natsConnection_QueueSubscribeSync(&sub, nc, subject.c_str(),
                queue_group.c_str());
        //natsConnection_SubscribeSync(&sub, nc, subject.c_str());
    }
/******************************************************************************
*Interface
******************************************************************************/
    int publish_task(task *task_t) override;
    task * subscribe_task_with_timeout( int &status) override;
    task * subscribe_task(int &status) override;
    int get_queue_count() override;
    int get_queue_size() override;
    int get_queue_count_limit() override;
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~NatsImpl(){}
};


#endif //AETRIO_MAIN_NATSCLIENT_H
