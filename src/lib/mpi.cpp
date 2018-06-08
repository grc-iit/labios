//
// Created by anthony on 4/24/18.
//
#include "mpi.h"
#include "../common/utilities.h"

int porus::MPI_Init(int *argc, char ***argv) {
  parse_opts(*argc,*argv);
  PMPI_Init(argc,argv);
  aetrio_system::getInstance(service::LIB);
  return 0;
}

void porus::MPI_Finalize() {
  PMPI_Finalize();
}