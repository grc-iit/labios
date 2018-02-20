//
// Created by anthony on 5/17/17.
//

#include <mpi.h>
#include "mpi_init.h"
#include "common/porus_system.h"
#include "core/request_handler/posix_rh_factory.h"
#include "../include/config_manager.h"
#include "common/constants.h"
#include "core/cache_manager/memcached_client.h"

/******************************************************************************
*Function MPI_Init
******************************************************************************/
int porus::MPI_Init( int *argc, char ***argv )
{
  int retval = PMPI_Init(argc, argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &porus_system::getInstance()->rank);

  if (retval == MPI_SUCCESS){
    porus_system::getInstance()->init();
    memcached_client::getInstance();
    split_comms(MPI_COMM_WORLD);
  }
  if (porus_system::getInstance()->is_client()){
      posix_api_request dummy;
      posix_rh_factory::getInstance()->get_rh(config_manager::getInstance()->POSIX_REQUEST_MODE)->accept(dummy);
  }
  return retval;
}
/******************************************************************************
*Function MPI_Finalize
******************************************************************************/
int porus::MPI_Finalize()
{
  int retval = PMPI_Finalize();
  if (retval == MPI_SUCCESS){
    if(porus_system::getInstance()->rank == 0){
      porus_system::getInstance()->destroy_request_dt();
      porus_system::getInstance()->destroy_response_dt();
    }
    destroy_comms();
  }

  return retval;
}
/******************************************************************************
*Function split_comms
******************************************************************************/
static void porus::split_comms(MPI_Comm comm)
{
  PMPI_Comm_dup(comm, &porus_system::getInstance()->MPI_COMM_ALL);
  PMPI_Comm_rank(porus_system::getInstance()->MPI_COMM_ALL,
                 &porus_system::getInstance()->rank);
  PMPI_Comm_size(porus_system::getInstance()->MPI_COMM_ALL,&porus_system::getInstance()
                     ->size);
  if (porus_system::getInstance()->is_client()){
    porus_system::getInstance()->groupkey = 0;

    PMPI_Comm_split (porus_system::getInstance()->MPI_COMM_ALL,
                     porus_system::getInstance()->groupkey,
                     porus_system::getInstance()->rank,
                     &porus_system::getInstance()->MPI_COMM_PORUS);
  }
  else{
    porus_system::getInstance()->groupkey = 1;
    PMPI_Comm_split (porus_system::getInstance()->MPI_COMM_ALL,
                     porus_system::getInstance()->groupkey,
                     porus_system::getInstance()->rank,
                     &porus_system::getInstance()->MPI_COMM_APPS);
  }
}
/******************************************************************************
*Function destroy_comms
******************************************************************************/
static void porus::destroy_comms()
{
  PMPI_Comm_free(&porus_system::getInstance()->MPI_COMM_ALL);
  PMPI_Comm_free(&porus_system::getInstance()->MPI_COMM_APPS);
  PMPI_Comm_free(&porus_system::getInstance()->MPI_COMM_PORUS);
  PMPI_Finalize();
}