//
// Created by hariharan on 2/16/18.
//

#ifndef PORUS_MAIN_POSIX_H
#define PORUS_MAIN_POSIX_H


#include <cstdio>
#include <cstring>
#include <mpi.h>
#include "../src/common/task_handler/task_handler.h"
#include "../src/common/metadata_manager/metadata_manager.h"
#include "../src/common/data_manager/data_manager.h"
#include "../src/System.h"
namespace porus{
    FILE *fopen(const char *filename, const char *mode);

    int fclose(FILE *stream);

    int fseek(FILE *stream, long int offset, int origin);

    size_t fread(void *ptr, size_t size, size_t count, FILE *stream);

    size_t fwrite(void *ptr, size_t size, size_t count, FILE *stream);
    int MPI_Init(int *argc, char ***argv);

    void MPI_Finalize();

    void MPI_Finalize();
}


#endif //PORUS_MAIN_POSIX_H
