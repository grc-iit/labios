#include <iostream>
#include "worker_service.h"

int main(int argc, char** argv) {
    int worker_index=atoi(argv[1]);
    std::shared_ptr<worker_service> service1=worker_service::getInstance(service::WORKER,worker_index);
    service1->run();
    return 0;
}