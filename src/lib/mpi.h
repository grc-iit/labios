//
// Created by anthony on 4/24/18.
//

#ifndef PORUS_MAIN_MPI_H
#define PORUS_MAIN_MPI_H
#include <mpi.h>
#include "../common/enumerations.h"
#include "../System.h"

namespace porus {
  int MPI_Init(int *argc, char ***argv);

  void MPI_Finalize();
}
#endif //PORUS_MAIN_MPI_H
