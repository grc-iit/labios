//
// Created by hdevarajan on 5/10/18.
//

#ifndef PORUS_MAIN_POSIXCLIENT_H
#define PORUS_MAIN_POSIXCLIENT_H


#include "../../common/data_structures.h"
#include "io_client.h"

class posix_client:public io_client {
public:
    posix_client():io_client(){}
    int write(write_task task) override ;
    int read(read_task task) override ;
    int delete_file(delete_task task) override ;
    int flush_file(flush_task task) override ;
};


#endif //PORUS_MAIN_POSIXCLIENT_H
