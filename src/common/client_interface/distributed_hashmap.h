/*******************************************************************************
* Created by hariharan on 2/23/18.
* Updated by akougkas on 6/29/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_DISTRIBUTEDHASHMAP_H
#define AETRIO_MAIN_DISTRIBUTEDHASHMAP_H
/******************************************************************************
*include files
******************************************************************************/
#include <cereal/types/memory.hpp>
#include "../enumerations.h"
#include "../constants.h"
#include "../exceptions.h"
/******************************************************************************
*Class
******************************************************************************/
class distributed_hashmap {
protected:
/******************************************************************************
*Variables and members
******************************************************************************/
    service service_i;
public:
/******************************************************************************
*Constructor
******************************************************************************/
    explicit distributed_hashmap(service service):service_i(service){}
/******************************************************************************
*Interface
******************************************************************************/
    virtual int put(
            const table &name, std::string key,
            const std::string &value,
            std::string group_key){
        throw NotImplementedException("put");
    }
    virtual std::string get(const table &name, std::string key,std::string group_key){
        throw NotImplementedException("get");
    }
    virtual std::string remove(const table &name, std::string key,std::string group_key){
        throw NotImplementedException("remove");
    }
    virtual bool exists(const table &name, std::string key,std::string group_key){
        throw NotImplementedException("remove");
    }
    virtual bool purge(){
        throw NotImplementedException("purge");
    }
    virtual size_t counter_init(const table &name, std::string key,std::string group_key){
        throw NotImplementedException("counter_init");
    }
    virtual size_t counter_inc(const table &name, std::string key,std::string
    group_key){
        throw NotImplementedException("counter_inc");
    }
    virtual size_t get_servers(){
        throw NotImplementedException("get_servers");
    }
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~distributed_hashmap(){}
};

#endif //AETRIO_MAIN_DISTRIBUTEDHASHMAP_H
