//
// Created by hdevarajan on 5/10/18.
//

#include "worker_service.h"
#include "../aetrio_system.h"
#include "program_repo/posix_client.h"

std::shared_ptr<worker_service> worker_service::instance = nullptr;

int worker_service::run() {
    std::shared_ptr<distributed_queue> queue=aetrio_system::getInstance(service_i)->worker_queue[worker_index];

    while(!kill){
        usleep(10);
        task task_i(task_type::WRITE_TASK);
        int status=queue->subscribe_task(task_i,CLIENT_TASK_SUBJECT);
        if(status!=-1){
           switch (task_i.t_type){
                case WRITE_TASK:{
                    write_task *wt= static_cast<write_task *>(&task_i);
                    client->write(*wt);
                    break;
                }
                case READ_TASK:{
                    read_task *rt= static_cast<read_task *>(&task_i);
                    client->read(*rt);
                    break;
                }
                case FLUSH_TASK:{
                    flush_task *ft= static_cast<flush_task *>(&task_i);
                    client->flush_file(*ft);
                    break;
                }
                case DELETE_TASK:{
                    delete_task *dt= static_cast<delete_task *>(&task_i);
                    client->delete_file(*dt);
                    break;
                }
            }
        }
    }
    return 0;
}
