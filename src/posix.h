//
// Created by anthony on 5/11/17.
//

#ifndef PORUS_POSIX_H
#define PORUS_POSIX_H

#include "api.h"
namespace porus{
/******************************************************************************
*Interface operations
******************************************************************************/
  FILE *fopen(const char *filename, const char *mode);

  int fclose(FILE *stream);

  int fseek(FILE *stream, long int offset, int origin);

  size_t fread(void *ptr, size_t size, size_t count, FILE *stream);

  size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);

}


#endif //PORUS_POSIX_H
