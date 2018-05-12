//
// Created by hariharan on 3/2/18.
//

#ifndef PORUS_MAIN_MEMCACHEDIMPL_H
#define PORUS_MAIN_MEMCACHEDIMPL_H


#include "../client_interface/distributed_hashmap.h"
#include <libmemcached/memcached.h>
#include <cstring>

class MemcacheDImpl: public distributed_hashmap {
private:
    memcached_st * mem_client;
public:
    MemcacheDImpl(service service,const std::string config_string,int server):distributed_hashmap(service){
       mem_client = memcached(config_string.c_str(), config_string.size());
    }
    int put(table table_name,std::string key,std::string value) override ;
    std::string get(table table_name, std::string key) override ;
    std::string remove(table table_name, std::string key) override ;
};


#endif //PORUS_MAIN_MEMCACHEDIMPL_H
