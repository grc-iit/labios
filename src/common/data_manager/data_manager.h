//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_DATA_MANAGER_H
#define PORUS_MAIN_DATA_MANAGER_H


#include <cereal/types/memory.hpp>
#include "../enumeration.h"

class data_manager {
private:
    static std::shared_ptr<data_manager> instance;
    data_manager(Service service){}
public:
    inline static std::shared_ptr<data_manager> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<data_manager>(new data_manager(service))
                                  : instance;
    }
    void* get(std::string);
    int put(std::string,void* data);
};


#endif //PORUS_MAIN_DATA_MANAGER_H