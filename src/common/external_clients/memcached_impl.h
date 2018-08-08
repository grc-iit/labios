/*******************************************************************************
* Created by hariharan on 2/3/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_MEMCACHEDIMPL_H
#define AETRIO_MAIN_MEMCACHEDIMPL_H
/******************************************************************************
*include files
******************************************************************************/
#include "../client_interface/distributed_hashmap.h"
#include <libmemcached/memcached.h>
#include <cstring>
/******************************************************************************
*Class
******************************************************************************/
class MemcacheDImpl: public distributed_hashmap {
/******************************************************************************
*Variables and members
******************************************************************************/
private:
    memcached_st * mem_client;
public:
/******************************************************************************
*Constructor
******************************************************************************/
    MemcacheDImpl(service service,const std::string &config_string,int server)
            :distributed_hashmap(service){
       mem_client = memcached(config_string.c_str(), config_string.size());
    }
/******************************************************************************
*Interface
******************************************************************************/
    int put(const table &name,std::string key,const std::string &value,std::string group_key) override;
    std::string get(const table &name, std::string key,std::string group_key) override;
    std::string remove(const table &name, std::string key,std::string group_key) override;
    bool exists(const table &name, std::string key,std::string group_key) override;
    bool purge() override;

    size_t counter_init(const table &name, std::string key,
                        std::string group_key) override;

    size_t counter_inc(const table &name, std::string key,
                       std::string group_key) override;

/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~MemcacheDImpl(){}
};


#endif //AETRIO_MAIN_MEMCACHEDIMPL_H
