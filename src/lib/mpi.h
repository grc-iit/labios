//
// Created by anthony on 4/24/18.
//

#ifndef AETRIO_MAIN_MPI_H
#define AETRIO_MAIN_MPI_H

#include <mpi.h>
#include "../common/enumerations.h"
#include "../aetrio_system.h"

namespace aetrio {
  int MPI_Init(int *argc, char ***argv);

  void MPI_Finalize();
}
#endif //AETRIO_MAIN_MPI_H
