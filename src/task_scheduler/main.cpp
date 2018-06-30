/*******************************************************************************
* Created by hariharan on 5/9/18.
* Updated by akougkas on 6/26/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include "../common/data_structures.h"
#include "task_scheduler.h"
#include "../common/utilities.h"
/******************************************************************************
*Main
******************************************************************************/
int main(int argc, char** argv) {
    parse_opts(argc,argv);
    MPI_Init(&argc,&argv);
    auto scheduler_service = task_scheduler::getInstance(TASK_SCHEDULER);
    scheduler_service->run();
    MPI_Finalize();
    return 0;
}