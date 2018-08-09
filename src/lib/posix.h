/*******************************************************************************
* Created by hariharan on 2/16/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_POSIX_H
#define AETRIO_MAIN_POSIX_H
/******************************************************************************
*include files
******************************************************************************/
#include <cstdio>
#include <cstring>
#include "mpi.h"
#include "../common/metadata_manager/metadata_manager.h"
#include "../common/data_manager/data_manager.h"
#include "../aetrio_system.h"
/******************************************************************************
*Aetrio Namespace
******************************************************************************/
namespace aetrio{
/******************************************************************************
*Interface
******************************************************************************/
    FILE *fopen(const char *filename, const char *mode);
    int fclose(FILE *stream);
    int fseek(FILE *stream, long int offset, int origin);
    size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
    std::vector<read_task> fread_async(size_t size, size_t count, FILE *stream);
    std::size_t fread_wait(void *ptr, std::vector<read_task> &tasks,
            std::string filename);
    std::vector<write_task*> fwrite_async(void *ptr, size_t size, size_t count,
                                          FILE *stream);
    size_t fwrite_wait(std::vector<write_task*> tasks);
    size_t fwrite(void *ptr, size_t size, size_t count, FILE *stream);
}


#endif //AETRIO_MAIN_POSIX_H
