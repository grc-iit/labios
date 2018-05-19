//
// Created by hariharan on 2/23/18.
//

#include "data_manager.h"

std::shared_ptr<data_manager> data_manager::instance = nullptr;
std::string data_manager::get(std::string key) {
    return aetrio_system::getInstance(service_i)->map_client->get(DATASPACE_DB,key);;
}

int data_manager::put(std::string key, std::string data) {

    return aetrio_system::getInstance(service_i)->map_client->put(DATASPACE_DB,key,data);;
}

bool data_manager::exists(std::string key) {
    return  aetrio_system::getInstance(service_i)->map_client->exists(DATASPACE_DB,key);
}

std::string data_manager::remove(table table_name, std::string key) {
    return aetrio_system::getInstance(service_i)->map_client->remove(DATASPACE_DB,key);
}
