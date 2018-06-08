#include <iostream>
#include <mpi.h>
#include "worker_manager_service.h"
#include "../common/utilities.h"

int main(int argc, char** argv) {
    parse_opts(argc,argv);
    MPI_Init(&argc,&argv);
    std::shared_ptr<worker_manager_service>
            worker_manager_service_i=worker_manager_service::getInstance
                    (service::WORKER_MANAGER);
    worker_manager_service_i->run();
    MPI_Finalize();
    return 0;
}