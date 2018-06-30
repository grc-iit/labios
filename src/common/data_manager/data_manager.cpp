/******************************************************************************
*include files
******************************************************************************/
#include "data_manager.h"
std::shared_ptr<data_manager> data_manager::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
std::string data_manager::get(const table &name, std::string key) {
    return aetrio_system::getInstance(service_i)->map_client->get
            (name,std::move(key));
}

int data_manager::put(const table &name, std::string key, std::string
data) {
    return aetrio_system::getInstance(service_i)->map_client->put
            (name,std::move(key),data);
}

bool data_manager::exists(const table &name, std::string key) {
    return  aetrio_system::getInstance(service_i)->map_client->exists
            (name,std::move(key));
}

std::string data_manager::remove(const table &name, std::string key) {
    return aetrio_system::getInstance(service_i)->map_client->remove
            (name,std::move(key));
}
