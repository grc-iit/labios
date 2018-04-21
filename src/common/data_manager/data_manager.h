//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_DATA_MANAGER_H
#define PORUS_MAIN_DATA_MANAGER_H


#include <cereal/types/memory.hpp>
#include "../enumeration.h"
#include "../external_clients/DistributedHashMap.h"
#include "../../System.h"

class data_manager {
private:
    static std::shared_ptr<data_manager> instance;
    DistributedHashMap* map;
    data_manager(Service service){
        map=System::getInstance(service)->map_client;
    }
public:
    inline static std::shared_ptr<data_manager> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<data_manager>(new data_manager(service))
                                  : instance;
    }
    std::string get(std::string);
    int put(std::string, std::string data);
    ~data_manager(){
        delete(map);
    }
};


#endif //PORUS_MAIN_DATA_MANAGER_H
