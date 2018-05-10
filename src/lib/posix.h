//
// Created by hariharan on 2/16/18.
//

#ifndef PORUS_MAIN_POSIX_H
#define PORUS_MAIN_POSIX_H


#include <cstdio>
#include <cstring>
#include "mpi.h"
#include "../common/task_handler/task_handler.h"
#include "../common/metadata_manager/metadata_manager.h"
#include "../common/data_manager/data_manager.h"
#include "../system.h"
namespace porus{
    FILE *fopen(const char *filename, const char *mode);

    int fclose(FILE *stream);

    int fseek(FILE *stream, long int offset, int origin);

    size_t fread(void *ptr, size_t size, size_t count, FILE *stream);

    size_t fwrite(void *ptr, size_t size, size_t count, FILE *stream);

}


#endif //PORUS_MAIN_POSIX_H
