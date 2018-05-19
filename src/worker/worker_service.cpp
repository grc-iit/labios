//
// Created by hdevarajan on 5/10/18.
//

#include "worker_service.h"
#include "../common/utilities.h"

std::shared_ptr<worker_service> worker_service::instance = nullptr;

int worker_service::run() {
    setup_working_dir();
    while(!kill){
        update_score(false);
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

int worker_service::update_score(bool before_sleeping=false) {
    int worker_score=calculate_worker_score(before_sleeping);
    std::cout<<"worker score: "<<worker_score<<std::endl;
    map->put(table::WORKER_SCORE,std::to_string(worker_index),std::to_string(worker_score));
}

int worker_service::calculate_worker_score(bool before_sleeping=false) {
    int pending_queue_size=queue->get_queue_count();
    int queue_size_limit=queue->get_queue_count_limit();
    float percentage_load=((float)pending_queue_size)/queue_size_limit;
    int64_t total_capacity=get_total_capacity();
    int64_t remaining_capacity=total_capacity-get_current_capacity();
    float percentage_capacity=((float)remaining_capacity)/total_capacity;
    float latency=0;
    if(before_sleeping) latency=0;
    else latency=1;
    float percentage_energy=((float)WORKER_ENERGY[worker_index-1])/5;
    float percentage_speed=((float)WORKER_SPEED[worker_index-1])/5;

    float final_score=  POLICY_WEIGHT[0]*percentage_load +
                        POLICY_WEIGHT[1]*percentage_capacity +
                        POLICY_WEIGHT[2]*latency +
                        POLICY_WEIGHT[3]*percentage_energy +
                        POLICY_WEIGHT[4]*percentage_speed;
    int worker_queue_category=-1;
    if(final_score >= 0 && final_score < .20){
        worker_queue_category=5;
    }else if(final_score >= .20 && final_score < .40){
        worker_queue_category=4;
    }else if(final_score >= .40 && final_score < .60){
        worker_queue_category=3;
    }else if(final_score >= .60 && final_score < .80){
        worker_queue_category=2;
    }else if(final_score >= .80 && final_score <= 1){
        worker_queue_category=1;
    }else{
        worker_queue_category=0;
    }
    return worker_queue_category;
}

int worker_service::update_capacity() {
    int64_t worker_capacity=get_total_capacity()-get_current_capacity();
    std::cout<<"worker capacity: "<<worker_capacity<<std::endl;
    map->put(table::WORKER_CAPACITY,std::to_string(worker_index),std::to_string(worker_capacity));
}

void worker_service::setup_working_dir() {
    std::string cmd="mkdir -p "+WORKER_PATH+"/"+std::to_string(worker_index)+"/";
    system(cmd.c_str());
    cmd="rm -rf "+WORKER_PATH+"/"+std::to_string(worker_index)+"/*";
    system(cmd.c_str());
}

int64_t worker_service::get_total_capacity() {
    return WORKER_CAPACITY_MAX[worker_index-1];
}

int64_t worker_service::get_current_capacity() {
    std::string cmd="du -s "+WORKER_PATH+"/"+std::to_string(worker_index)+"/"+" | awk {'print$1'}";
    FILE *fp;
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
            result += buffer.data();
    }
    return std::stoll(result);
}
