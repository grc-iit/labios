/*******************************************************************************
* Created by hariharan on 5/12/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_CONFIGURATION_MANAGER_H
#define AETRIO_CONFIGURATION_MANAGER_H
/******************************************************************************
*include files
******************************************************************************/
#include <memory>
#include <thread>

/******************************************************************************
*Class
******************************************************************************/
class config_manager {
private:
    static std::shared_ptr<config_manager> instance;
/******************************************************************************
*Constructor
******************************************************************************/
    config_manager():
            NATS_URL_CLIENT("nats://localhost:4222/"),
            NATS_URL_SERVER("nats://localhost:4223/"),
            MEMCACHED_URL_CLIENT("--SERVER=localhost:11211"),
            MEMCACHED_URL_SERVER("--SERVER=localhost:11212"),
            ASSIGNMENT_POLICY("RANDOM"),
            TS_NUM_WORKER_THREADS(1){}
public:
/******************************************************************************
*Variables and members
******************************************************************************/
    std::string NATS_URL_CLIENT;
    std::string NATS_URL_SERVER;
    std::string MEMCACHED_URL_CLIENT;
    std::string MEMCACHED_URL_SERVER;
    std::string ASSIGNMENT_POLICY;
    int TS_NUM_WORKER_THREADS;
    static std::shared_ptr<config_manager> get_instance(){
        return instance== nullptr ? instance=std::shared_ptr<config_manager>
                (new config_manager()) : instance;
    }
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~config_manager(){}
};


#endif //AETRIO_CONFIGURATION_MANAGER_H


