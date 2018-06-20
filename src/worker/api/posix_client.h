//
// Created by hdevarajan on 5/10/18.
//

#ifndef AETRIO_MAIN_POSIXCLIENT_H
#define AETRIO_MAIN_POSIXCLIENT_H


#include "../../common/data_structures.h"
#include "io_client.h"
#include <chrono>
using namespace std::chrono;

class posix_client:public io_client {
    std::string dir;
public:
    posix_client(int worker_index):io_client(worker_index),dir(WORKER_PATH+"/"+std::to_string(worker_index)+"/"){}
    int write(write_task task) override ;
    int read(read_task task) override ;
    int delete_file(delete_task task) override ;
    int flush_file(flush_task task) override ;
};


#endif //AETRIO_MAIN_POSIXCLIENT_H
