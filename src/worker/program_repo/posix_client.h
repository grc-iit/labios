//
// Created by hdevarajan on 5/10/18.
//

#ifndef PORUS_MAIN_POSIXCLIENT_H
#define PORUS_MAIN_POSIXCLIENT_H


#include "../../common/data_structures.h"

class PosixClient {
public:
    int write(write_task task);
    int read(read_task task);
    int delete_file(read_task task);
    int flush_file(read_task task);
};


#endif //PORUS_MAIN_POSIXCLIENT_H
