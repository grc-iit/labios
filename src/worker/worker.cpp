/******************************************************************************
*include files
******************************************************************************/
#include "worker.h"
#include "../common/utilities.h"
#include "../common/return_codes.h"
std::shared_ptr<worker> worker::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
int worker::run() {
    if(!setup_working_dir()==SUCCESS){
        throw std::runtime_error("worker::setup_working_dir() failed!");
    }
    if(!update_score(false)==SUCCESS){
        throw std::runtime_error("worker::update_score() failed!");
    }
    if(!update_capacity()==SUCCESS){
        throw std::runtime_error("worker::update_capacity() failed!");
    }
    int task_count=0;
    Timer t=Timer();
    t.startTime();

    do{
        int status=-1;
        task* task_i= queue->subscribe_task(status);

        if(status!=-1 && task_i!= nullptr){
            task_count++;
            switch (task_i->t_type){
                case task_type::WRITE_TASK:{
                    auto *wt= reinterpret_cast<write_task *>(task_i);
                    std::cout<< serialization_manager().serialize_task(wt) << std::endl;
                    client->write(*wt);
                    break;
                }
                case task_type::READ_TASK:{
                    auto *rt= reinterpret_cast<read_task *>(task_i);
                    std::cout<< serialization_manager().serialize_task(rt) << std::endl;
                    client->read(*rt);
                    break;
                }
                case task_type::FLUSH_TASK:{
                    auto *ft= reinterpret_cast<flush_task *>(task_i);
                    client->flush_file(*ft);
                    break;
                }
                case task_type::DELETE_TASK:{
                    auto *dt= reinterpret_cast<delete_task *>(task_i);
                    client->delete_file(*dt);
                    break;
                }
                default:
                    std::cerr<<"Task #"<<task_i->task_id<< " type unknown\n";
                    throw std::runtime_error("worker::run() failed!");
            }
        }
        if(t.stopTime()>WORKER_INTERVAL || task_count>MAX_WORKER_TASK_COUNT){
            update_score(false);
            update_capacity();
            t.startTime();
            task_count=0;
        }
    }while(!kill);

    return 0;
}

int worker::update_score(bool before_sleeping=false) {
    int worker_score = calculate_worker_score(before_sleeping);
    std::cout<<"worker score: "<<worker_score<<std::endl;
    if(worker_score){
        if(!map->put(
                table::WORKER_SCORE,
                std::to_string(worker_index),
                std::to_string(worker_score))==MEMCACHED_SUCCESS){
            return WORKER__UPDATE_SCORE_FAILED;
        }
    }
    return SUCCESS;
}

int worker::calculate_worker_score(bool before_sleeping=false) {
    float load = (float)queue->get_queue_count()/INT_MAX;
    float capacity=get_remaining_capacity()/get_total_capacity();
    float isAlive= before_sleeping?0:1;
    float energy=((float)WORKER_ENERGY[worker_index-1])/5;
    float speed=((float)WORKER_SPEED[worker_index-1])/5;

    float score= POLICY_WEIGHT[0]*load +
                 POLICY_WEIGHT[1]*capacity +
                 POLICY_WEIGHT[2]*isAlive +
                 POLICY_WEIGHT[3]*energy +
                 POLICY_WEIGHT[4]*speed;

    int worker_score=-1;
    if(score >= 0 && score < .20){
        worker_score=5;//worker: relatively full, busy, slow, high-energy
    }else if(score >= .20 && score < .40){
        worker_score=4;
    }else if(score >= .40 && score < .60){
        worker_score=3;
    }else if(score >= .60 && score < .80){
        worker_score=2;
    }else if(score >= .80 && score <= 1){
        worker_score=1;//worker: relatively empty, not busy, speedy, efficient
    }else{
        worker_score=0;
    }
    return worker_score;
}

int worker::update_capacity() {
    if(map->put(
            table::WORKER_CAPACITY,
            std::to_string(worker_index),
            std::to_string(get_remaining_capacity()))==MEMCACHED_SUCCESS){
        std::cout<<"worker capacity: "<<(int)get_remaining_capacity()<<std::endl;
        return SUCCESS;
    }
    else return WORKER__UPDATE_CAPACITY_FAILED;
}

int worker::setup_working_dir() {
    std::string cmd="mkdir -p "+WORKER_PATH+"/"+std::to_string(worker_index)+"/";
    if(system(cmd.c_str())){
        cmd="rm -rf "+WORKER_PATH+"/"+std::to_string(worker_index)+"/*";
        if(system(cmd.c_str())) return SUCCESS;
    }
    return WORKER__SETTING_DIR_FAILED;
}

int64_t worker::get_total_capacity() {
    return WORKER_CAPACITY_MAX[worker_index-1];
}

int64_t worker::get_current_capacity() {
    std::string cmd="du -s "+WORKER_PATH+"/"+std::to_string(worker_index)+"/"+" | awk {'print$1'}";
    FILE *fp;
    std::array<char, 128> buffer = std::array<char, 128>();
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
            result += buffer.data();
    }
    return std::stoll(result);
}

float worker::get_remaining_capacity() {
    return (float)(get_total_capacity() - get_current_capacity());
}
