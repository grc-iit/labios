//
// Created by hdevarajan on 5/10/18.
//

#ifndef PORUS_MAIN_IO_CLIENT_H
#define PORUS_MAIN_IO_CLIENT_H

#include "../../common/data_structures.h"

class io_client {
public:
    virtual int write(write_task task)=0;
    virtual int read(read_task task)=0;
    virtual int delete_file(delete_task task)=0;
    virtual int flush_file(flush_task task)=0;
};
#endif //PORUS_MAIN_IO_CLIENT_H
