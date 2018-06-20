//
// Created by hariharan on 2/16/18.
//

#ifndef AETRIO_MAIN_SYSTEM_H
#define AETRIO_MAIN_SYSTEM_H


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
#include "common/configuration_manager.h"
#include <mpi.h>
#include <string>

class aetrio_system {
private:
    static std::shared_ptr<aetrio_system> instance;
    int application_id;
    service service_i;
    explicit aetrio_system(service service)
            :service_i(service),application_id(),
             client_comm(),client_rank(),rank(){
        init(service_i);
    }

    void init(service service);

public:
    inline std::shared_ptr<distributed_queue>
            get_queue_client(const std::string &subject){
        return std::make_shared<NatsImpl>
                (service_i, configuration_manager::get_instance()
                        ->NATS_URL_CLIENT,CLIENT_TASK_SUBJECT);
    }
    inline std::shared_ptr<distributed_queue>
            get_worker_queue(const int &worker_index){
        return std::make_shared<NatsImpl>
                (service_i,configuration_manager::get_instance()
                        ->NATS_URL_SERVER,WORKER_TASK_SUBJECT[worker_index]);
    }
    std::shared_ptr<solver> solver_i;
    std::shared_ptr<distributed_hashmap> map_client,map_server;
    int rank,client_rank;
    MPI_Comm client_comm;
    inline static std::shared_ptr<aetrio_system> getInstance(service service){
        return instance== nullptr ? instance=std::shared_ptr<aetrio_system>
                (new aetrio_system(service))
                                  : instance;
    }

    int build_message_key(MPI_Datatype &message);
    int build_message_file(MPI_Datatype &message_file);
    int build_message_chunk(MPI_Datatype &message_chunk);
};


#endif //AETRIO_MAIN_SYSTEM_H
