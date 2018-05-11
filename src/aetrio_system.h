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
#include "common/external_clients/nats_impl.h"
#include <mpi.h>
#include <string>
class aetrio_system {
private:
    static std::shared_ptr<aetrio_system> instance;
    int application_id;
    service service_i;
    aetrio_system(service service):service_i(service){
        init(service);
    }

    int init(service service);
public:
    inline std::shared_ptr<distributed_queue> get_queue_client(std::string subject){
        return std::shared_ptr<NatsImpl>(new NatsImpl(service_i,NATS_URL_CLIENT,CLIENT_TASK_SUBJECT));
    }
    inline std::shared_ptr<distributed_queue> get_worker_queue(int worker_index, std::string subject){
        return std::shared_ptr<NatsImpl>(new NatsImpl(service_i,NATS_URL_SERVER,WORKER_TASK_SUBJECT[worker_index]));
    }
    std::shared_ptr<solver> solver_i;
    std::shared_ptr<distributed_hashmap> map_client,map_server;
    int rank,client_rank;
    MPI_Comm client_comm;
    inline static std::shared_ptr<aetrio_system> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<aetrio_system>(new aetrio_system(service))
                                  : instance;
    }

    int build_message_key(MPI_Datatype &message);
    int build_message_file(MPI_Datatype &message_file);
    int build_message_chunk(MPI_Datatype &message_chunk);
};


#endif //PORUS_MAIN_SYSTEM_H
