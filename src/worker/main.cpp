#include <iostream>
#include <mpi.h>
#include "worker_service.h"
#include "../common/utilities.h"

int main(int argc, char** argv) {
    parse_opts(argc,argv);
    MPI_Init(&argc,&argv);
    int worker_index=atoi(argv[1]);
    std::shared_ptr<worker_service>
            worker_service_i=worker_service::getInstance(service::WORKER,
                    worker_index);
    worker_service_i->run();
    MPI_Finalize();
    return 0;
}