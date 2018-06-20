//
// Created by hariharan on 2/23/18.
//

#ifndef AETRIO_MAIN_DATA_MANAGER_H
#define AETRIO_MAIN_DATA_MANAGER_H


#include <cereal/types/memory.hpp>
#include "../enumerations.h"
#include "../client_interface/distributed_hashmap.h"
#include "../../aetrio_system.h"

class data_manager {
private:
    static std::shared_ptr<data_manager> instance;
    service service_i;
    data_manager(service service):service_i(service){

    }
public:
    inline static std::shared_ptr<data_manager> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<data_manager>(new data_manager(service))
                                  : instance;
    }
    std::string get(std::string);
    int put(std::string, std::string data);
    bool exists(std::string key);
    std::string remove(table table_name, std::string key);
    ~data_manager(){
    }
};


#endif //AETRIO_MAIN_DATA_MANAGER_H
