//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_DATA_MANAGER_H
#define PORUS_MAIN_DATA_MANAGER_H


#include <cereal/types/memory.hpp>
#include "../enumerations.h"
#include "../client_interface/distributed_hashmap.h"
#include "../../system.h"

class data_manager {
private:
    static std::shared_ptr<data_manager> instance;
    Service service;
    data_manager(Service service):service(service){

    }
public:
    inline static std::shared_ptr<data_manager> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<data_manager>(new data_manager(service))
                                  : instance;
    }
    std::string get(std::string);
    int put(std::string, std::string data);
    ~data_manager(){
    }
};


#endif //PORUS_MAIN_DATA_MANAGER_H
