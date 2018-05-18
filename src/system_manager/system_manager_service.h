//
// Created by hdevarajan on 5/14/18.
//

#ifndef PORUS_MAIN_SYSTEM_MANAGER_SERVICE_H
#define PORUS_MAIN_SYSTEM_MANAGER_SERVICE_H


#include <memory>
#include "../common/enumerations.h"

class system_manager_service {
    static std::shared_ptr<system_manager_service> instance;
    service service_i;
    system_manager_service(service service):service_i(service),kill(false){}
    int check_applications_score();
public:
    int kill;

    inline static std::shared_ptr<system_manager_service> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<system_manager_service>(new system_manager_service(service))
                                  : instance;
    }
    int run();
};


#endif //PORUS_MAIN_SYSTEM_MANAGER_SERVICE_H
