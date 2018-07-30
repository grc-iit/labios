/******************************************************************************
*include files
******************************************************************************/
#include "nats_impl.h"
#include "../timer.h"

/******************************************************************************
*Interface
******************************************************************************/
int NatsImpl::publish_task(task* task_t) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    auto msg = serialization_manager().serialize_task(task_t);
    natsConnection_PublishString(nc, subject.c_str(), msg.c_str());
#ifdef TIMER
    std::cout<<"NatsImpl::publish_task(),"<<t.pauseTime()<<"\n";
#endif
    return 0;
}

task*  NatsImpl::subscribe_task_with_timeout(int &status) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    natsMsg *msg = nullptr;
    natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER_MS);
    if(msg==nullptr) return nullptr;
    task* task= serialization_manager().deserialize_task(natsMsg_GetData(msg));
    status=0;
#ifdef TIMER
    std::cout<<"NatsImpl::subscribe_task_with_timeout(),"<<t.pauseTime()<<"\n";
#endif
    return task;
}

task* NatsImpl::subscribe_task(int &status) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    natsMsg *msg = nullptr;
    natsSubscription_NextMsg(&msg, sub, MAX_TASK_TIMER_MS_MAX);
    if(msg==nullptr) return nullptr;
    task* task= serialization_manager().deserialize_task(natsMsg_GetData(msg));
    status=0;
#ifdef TIMER
    std::cout<<"NatsImpl::subscribe_task(),"<<t.pauseTime()<<"\n";
#endif
    return task;
}

int NatsImpl::get_queue_size() {
    int size_of_queue;
    natsSubscription_GetPending(sub,nullptr,&size_of_queue);
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