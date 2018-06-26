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
    int put(table table_name,std::string key,std::string value) override ;
    std::string get(table table_name, std::string key) override ;
    std::string remove(table table_name, std::string key) override ;
    bool exists(table table_name, std::string key) override;
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~MemcacheDImpl(){}
};


#endif //AETRIO_MAIN_MEMCACHEDIMPL_H
