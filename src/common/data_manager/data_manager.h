//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_DATA_MANAGER_H
#define PORUS_MAIN_DATA_MANAGER_H


#include <cereal/types/memory.hpp>
#include "../enumerations.h"
#include "../client_interface/distributed_hashmap.h"
#include "../../porus_system.h"

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
    ~data_manager(){
    }
};


#endif //PORUS_MAIN_DATA_MANAGER_H
