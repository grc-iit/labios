//
// Created by hdevarajan on 5/14/18.
//

#ifndef AETRIO_MAIN_WORKER_MANAGER_SERVICE_H
#define AETRIO_MAIN_WORKER_MANAGER_SERVICE_H


#include <memory>
#include "../common/enumerations.h"

class worker_manager_service {
    static std::shared_ptr<worker_manager_service> instance;
    service service_i;
    worker_manager_service(service service):service_i(service),kill(false){
    }

    int sort_worker_score();
public:
    int kill;
    inline static std::shared_ptr<worker_manager_service> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<worker_manager_service>(new worker_manager_service(service))
                                  : instance;
    }
    void run();
};


#endif //AETRIO_MAIN_WORKER_MANAGER_SERVICE_H
