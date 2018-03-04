//
// Created by hariharan on 2/16/18.
//

#ifndef PORUS_MAIN_SYSTEM_H
#define PORUS_MAIN_SYSTEM_H


#include <memory>
#include <climits>
#include "common/enumeration.h"
#include "common/constants.h"
#include "common/external_clients/DistributedHashMap.h"
#include "common/external_clients/MemcacheDImpl.h"
#include "common/external_clients/RocksDBImpl.h"
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
    DistributedHashMap* map_client,*map_server;
    int rank,client_rank;
    MPI_Comm client_comm;
    inline static std::shared_ptr<System> getInstance(Service service){
        return instance== nullptr ? instance=std::shared_ptr<System>(new System(service))
                                  : instance;
    }

    int build_message_key(MPI_Datatype &message);
    int build_message_file(MPI_Datatype &message_file);
    int build_message_chunk(MPI_Datatype &message_chunk);
    std::string get_task_subject();
};


#endif //PORUS_MAIN_SYSTEM_H
