/*******************************************************************************
* Created by hariharan on 2/23/18.
* Updated by akougkas on 6/29/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_DATA_MANAGER_H
#define AETRIO_MAIN_DATA_MANAGER_H
/******************************************************************************
*include files
******************************************************************************/
#include <cereal/types/memory.hpp>
#include "../enumerations.h"
#include "../client_interface/distributed_hashmap.h"
#include "../../aetrio_system.h"
/******************************************************************************
*Class
******************************************************************************/
class data_manager {
private:
/******************************************************************************
*Variables and members
******************************************************************************/
    static std::shared_ptr<data_manager> instance;
    service service_i;
/******************************************************************************
*Constructor
******************************************************************************/
    explicit data_manager(service service):service_i(service){}
public:
/******************************************************************************
*Interface
******************************************************************************/
    inline static std::shared_ptr<data_manager> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<data_manager>
                (new data_manager(service)) : instance;
    }
    std::string get(const table &name, std::string key);
    int put(const table &name, std::string key, std::string data);
    bool exists(const table &name, std::string key);
    std::string remove(const table &name, std::string key);
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~data_manager(){}
};
#endif //AETRIO_MAIN_DATA_MANAGER_H
