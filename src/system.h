//
// Created by hariharan on 2/16/18.
//

#ifndef PORUS_MAIN_SYSTEM_H
#define PORUS_MAIN_SYSTEM_H


#include <memory>
#include <climits>
#include "common/enumerations.h"
#include "common/constants.h"
#include "common/client_interface/distributed_hashmap.h"
#include "common/external_clients/memcached_impl.h"
#include "common/external_clients/rocksdb_impl.h"
#include "common/client_interface/distributed_queue.h"
#include "common/solver/solver.h"
#include <mpi.h>
#include <string>
class System {
private:
    static std::shared_ptr<System> instance;
    int application_id;
    Service service;
    System(Service service):service(service){
        init(service);

    }

    int init(Service service);
public:
    std::shared_ptr<solver> solver;
    std::shared_ptr<distributed_hashmap> map_client,map_server;
    std::shared_ptr<DistributedQueue> queue_client;
    std::shared_ptr<DistributedQueue> worker_queue[MAX_WORKER_COUNT];
    int rank,client_rank;
    MPI_Comm client_comm;
    inline static std::shared_ptr<System> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<System>(new System(service))
                                  : instance;
    }

    int build_message_key(MPI_Datatype &message);
    int build_message_file(MPI_Datatype &message_file);
    int build_message_chunk(MPI_Datatype &message_chunk);
};


#endif //PORUS_MAIN_SYSTEM_H
