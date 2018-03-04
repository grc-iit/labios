//
// Created by hariharan on 2/23/18.
//

#include "data_manager.h"
std::shared_ptr<data_manager> data_manager::instance = nullptr;
void *data_manager::get(std::string) {
    return nullptr;
}

int data_manager::put(std::string, void *data) {
    return 0;
}
