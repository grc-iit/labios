//
// Created by anthony on 5/17/17.
//

#ifndef PORUS_MPI_H
#define PORUS_MPI_H

namespace porus {
/******************************************************************************
*Function declarations
******************************************************************************/
  static void split_comms(MPI_Comm);
  static void destroy_comms();
  int MPI_Init(int *argc, char ***argv);
  int MPI_Finalize();
}


#endif //PORUS_MPI_H
