//
// Created by hdevarajan on 5/10/18.
//

#include "worker_service.h"

std::shared_ptr<worker_service> worker_service::instance = nullptr;

int worker_service::run() {
    setup_working_dir();
    while(!kill){
        update_score();
        update_capacity();
        usleep(10);
        int status=-1;
        task* task_i= queue->subscribe_task(status);
        if(status!=-1){
           switch (task_i->t_type){
                case task_type::WRITE_TASK:{
                    write_task *wt= static_cast<write_task *>(task_i);
                    std::cout<< serialization_manager().serialise_task(wt) << std::endl;
                    client->write(*wt);
                    break;
                }
                case task_type::READ_TASK:{
                    read_task *rt= static_cast<read_task *>(task_i);
                    std::cout<< serialization_manager().serialise_task(rt) << std::endl;
                    client->read(*rt);
                    break;
                }
                case task_type::FLUSH_TASK:{
                    flush_task *ft= static_cast<flush_task *>(task_i);
                    client->flush_file(*ft);
                    break;
                }
                case task_type::DELETE_TASK:{
                    delete_task *dt= static_cast<delete_task *>(task_i);
                    client->delete_file(*dt);
                    break;
                }
            }
        }
    }
    return 0;
}

int worker_service::update_score() {
    int worker_score=calculate_worker_score();
    map->put(table::WORKER_SCORE,std::to_string(worker_index),std::to_string(worker_score));
}

int worker_service::calculate_worker_score() {
    int pending_queue_size=queue->get_queue_count();
    int queue_size_limit=queue->get_queue_count_limit();
    float percentage=((float)pending_queue_size)/queue_size_limit;
    int worker_queue_category=-1;
    if(percentage >= 0 && percentage < .20){
        worker_queue_category=5;
    }else if(percentage >= 20 && percentage < 40){
        worker_queue_category=4;
    }else if(percentage >= 40 && percentage < 60){
        worker_queue_category=3;
    }else if(percentage >= 60 && percentage < 80){
        worker_queue_category=2;
    }else if(percentage >= 80 && percentage < 100){
        worker_queue_category=1;
    }else{
        worker_queue_category=0;
    }
    return worker_queue_category;
}

int worker_service::update_capacity() {
    int worker_capacity=4*1024*1024;
    map->put(table::WORKER_CAPACITY,std::to_string(worker_index),std::to_string(worker_capacity));
}

void worker_service::setup_working_dir() {
    std::string cmd="mkdir -p "+WORKER_PATH+"/"+std::to_string(worker_index)+"/";
    system(cmd.c_str());
    cmd="rm -rf "+WORKER_PATH+"/"+std::to_string(worker_index)+"/*";
    system(cmd.c_str());
}
