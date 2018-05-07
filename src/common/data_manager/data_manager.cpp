//
// Created by hariharan on 2/23/18.
//

#include "data_manager.h"

std::shared_ptr<data_manager> data_manager::instance = nullptr;
std::string data_manager::get(std::string key) {
    return System::getInstance(service)->map_client->get(DATASPACE_DB,key);;
}

int data_manager::put(std::string key, std::string data) {

    return System::getInstance(service)->map_client->put(DATASPACE_DB,key,data);;
}
