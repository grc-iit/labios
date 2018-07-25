//
// Created by anthony on 4/24/18.
//
#include "mpi.h"
#include "../common/utilities.h"

int aetrio::MPI_Init(int *argc, char ***argv) {
  PMPI_Init(argc,argv);
  parse_opts(*argc,*argv);
  aetrio_system::getInstance(service::LIB);
  return 0;
}

void aetrio::MPI_Finalize() {
  PMPI_Finalize();
}