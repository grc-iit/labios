//
// Created by hariharan on 2/16/18.
//

#include "System.h"
#include "common/external_clients/NatsImpl.h"
#include "common/solver/DPSolver.h"
#include "common/solver/GreedySolver.h"

std::shared_ptr<System> System::instance = nullptr;

int System::init(Service service) {
    MPI_Comm_rank(MPI_COMM_SELF,&rank);
    if(map_impl_type_t==map_impl_type::MEMCACHE_D){
        map_server= std::shared_ptr<MemcacheDImpl>(new MemcacheDImpl(service,MEMCACHED_URL_SERVER,0));
    }else if(map_impl_type_t==map_impl_type::ROCKS_DB){
        map_server=  std::shared_ptr<RocksDBImpl>(new RocksDBImpl(service,kDBPath_server));
    }
    if(solver_impl_type_t==solver_impl_type::DP){
        solver=std::shared_ptr<DPSolver>(new DPSolver(service));
    }else if(solver_impl_type_t==solver_impl_type::GREEDY){
        solver=std::shared_ptr<GreedySolver>(new GreedySolver(service));
    }
    switch(service){
        case LIB:{
            if(rank==0){
                auto value=map_server->get(table::SYSTEM_REG,"app_no");
                int curr=0;
                if(strcmp(value.c_str(),"")!=0){
                    curr=atoi(value.c_str());
                    curr++;
                }
                application_id=curr;
                map_server->put(table::SYSTEM_REG,"app_no",std::to_string(curr));
            }
            break;
        }
        case CLIENT:{
            break;
        }
        case SYSTEM_MANAGER:{
            break;
        }
        case TASK_SCHEDULER:{
            break;
        }
        case WORKER_MANAGER:{
            break;
        }
        case WORKER:{
            break;
        }
    }

    if(map_impl_type_t==map_impl_type::MEMCACHE_D){
        map_client= std::shared_ptr<MemcacheDImpl>(new MemcacheDImpl(service,MEMCACHED_URL_CLIENT,application_id));

    }else if(map_impl_type_t==map_impl_type::ROCKS_DB){
        map_client=  std::shared_ptr<RocksDBImpl>(new RocksDBImpl(service,kDBPath_client));
    }
    if(queue_impl_type_t==queue_impl_type::NATS){
        queue_client=std::shared_ptr<NatsImpl>(new NatsImpl(service,NATS_URL_CLIENT));
    }
    return 0;
}

int System::build_message_key(MPI_Datatype &message) {
    MPI_Datatype  type[4] = {MPI_INT, MPI_INT, MPI_INT,MPI_CHAR};
    int blocklen[4] = {1, 1,1, KEY_SIZE};
    MPI_Aint disp[4]={0, sizeof(MPI_INT), 2*sizeof(MPI_INT), 3*sizeof(MPI_INT)};
    MPI_Type_struct(8, blocklen, disp, type, &message);
    MPI_Type_commit(&message);
    return 0;
}

int System::build_message_file(MPI_Datatype &message_file) {
    MPI_Datatype  type[3] = {MPI_CHAR, MPI_INT, MPI_INT};
    int blocklen[3] = {KEY_SIZE,1, 1, };
    MPI_Aint disp[3]={0, KEY_SIZE*sizeof(MPI_CHAR), KEY_SIZE*sizeof(MPI_CHAR)+sizeof(MPI_INT)};
    MPI_Type_struct(6, blocklen, disp, type, &message_file);
    MPI_Type_commit(&message_file);
    return 0;
}

int System::build_message_chunk(MPI_Datatype &message_chunk) {
    MPI_Datatype  type[5] = {MPI_INT,MPI_INT,MPI_CHAR,MPI_INT,MPI_INT};
    int blocklen[5] = {1, 1,FILE_SIZE,1,1 };
    MPI_Aint disp[5]={0, sizeof(MPI_INT),2*sizeof(MPI_INT),2*sizeof(MPI_INT)+FILE_SIZE*sizeof(MPI_CHAR),3*sizeof(MPI_INT)+FILE_SIZE*sizeof(MPI_CHAR)};
    MPI_Type_struct(10, blocklen, disp, type, &message_chunk);
    MPI_Type_commit(&message_chunk);
    return 0;
}

std::string System::get_task_subject() {
    return TASK_SUBJECT;
}
