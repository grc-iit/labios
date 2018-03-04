//
// Created by hariharan on 3/2/18.
//

#ifndef PORUS_MAIN_MEMCACHEDIMPL_H
#define PORUS_MAIN_MEMCACHEDIMPL_H


#include "DistributedHashMap.h"
#include <libmemcached/memcached.h>

class MemcacheDImpl: public DistributedHashMap {
private:
    memcached_st * mem_client;
    int application_id;
public:
    MemcacheDImpl(Service service,std::string config_string,int server):DistributedHashMap(service),application_id(server){
        mem_client = memcached(config_string.c_str(), config_string.length());
    }
    int put(table table_name,std::string key,std::string value) override ;
    std::string get(table table_name, std::string key) override ;
    std::string remove(table table_name, std::string key) override ;
};


#endif //PORUS_MAIN_MEMCACHEDIMPL_H
