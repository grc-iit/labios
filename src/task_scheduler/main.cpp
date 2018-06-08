


#include <mpi.h>
#include "../common/data_structures.h"
#include "../common/solver/dp_solver.h"
#include "task_scheduler_service.h"
#include "../common/utilities.h"

int main(int argc, char** argv) {
    parse_opts(argc,argv);
    MPI_Init(&argc,&argv);
    std::shared_ptr<task_scheduler_service> scheduler_service=task_scheduler_service::getInstance(TASK_SCHEDULER);
    scheduler_service->run();
    MPI_Finalize();
    return 0;
}