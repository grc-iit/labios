/*******************************************************************************
* WORKER MAIN
* Created by hariharan on 5/10/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
/******************************************************************************
*include files
******************************************************************************/
#include <iostream>
#include <mpi.h>
#include "worker.h"
#include "../common/utilities.h"
/******************************************************************************
*Worker main
******************************************************************************/
int main(int argc, char** argv) {
    MPI_Init(&argc,&argv);
    parse_opts(argc,argv);
    int worker_index=atoi(argv[1]);
    std::shared_ptr<worker> worker_service_i =
            worker::getInstance(service::WORKER, worker_index);
    worker_service_i->run();
    MPI_Finalize();
    return 0;
}