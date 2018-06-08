//
// Created by hdevarajan on 5/12/18.
//

#ifndef VMAP_CONFIGURATION_MANAGER_H
#define VMAP_CONFIGURATION_MANAGER_H


#include <memory>

class configuration_manager {
private:
    static std::shared_ptr<configuration_manager> instance;
    configuration_manager():NATS_URL_CLIENT("nats://localhost:4222/"),
            NATS_URL_SERVER("nats://localhost:4223/"),
            MEMCACHED_URL_CLIENT("--SERVER=localhost:11211"),
            MEMCACHED_URL_SERVER("--SERVER=localhost:11212"){}
public:
    std::string NATS_URL_CLIENT;
    std::string NATS_URL_SERVER;
    std::string MEMCACHED_URL_CLIENT;
    std::string MEMCACHED_URL_SERVER;
    static std::shared_ptr<configuration_manager> get_instance(){
        return instance== nullptr ? instance=std::shared_ptr<configuration_manager>(new configuration_manager()) : instance;
    }
};


#endif //VMAP_CONFIGURATION_MANAGER_H


