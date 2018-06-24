//
// Created by hariharan on 2/17/18.
//

#ifndef AETRIO_MAIN_STRUCTURE_H
#define AETRIO_MAIN_STRUCTURE_H

#include <cereal/types/memory.hpp>
#include "enumerations.h"
#include "constants.h"
#include <cereal/types/string.hpp>
#include <cereal/types/common.hpp>
#include <utility>


struct message_key{
    message_type m_type;
    map_type mp_type;
    operation operation_type;
    char key[KEY_SIZE];

};
struct message{
    message_type m_type;
    map_type mp_type;
    char key[KEY_SIZE];
    size_t data_size;
    char* data;
};
struct file{
    source_type dest_t;
    std::string filename;
    int64_t offset;
    std::size_t size;
    int64_t worker=-1;
    file(std::string filename, long long int offset, size_t file_size)
            :filename(std::move(filename)),offset(offset),size(file_size),
             dest_t(DATASPACE_LOC){}

    //file(const file &file_t) = default;
    file(const file &file_t)
            :filename(file_t.filename),offset(file_t.offset),size(file_t.size),dest_t(file_t.dest_t),worker(file_t.worker){}
    //file() {};

    file() : dest_t(DATASPACE_LOC), filename(""), offset(0), size(0){}

    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( this->filename,this->offset,this->size,this->dest_t,this->worker);
    }
};
struct chunk_meta{
    file actual_user_chunk;
    file destination;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( this->actual_user_chunk,this->destination );
    }

};
struct chunk_msg{
    source_type chunkType;
    std::string dataspace_id;
    std::string filename;
    size_t offset;
    size_t file_size;

};
struct file_meta{
    file file_struct;
    std::vector<chunk_meta> chunks;
    file_meta():chunks(){}
};
struct dataspace{
    size_t size;
    void* data;
};

struct file_stat{
    FILE* fh;
    long long int file_pointer;
    size_t file_size;
    std::string mode;
    bool is_open;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( /*this->fh,*/ this->file_pointer, this->file_size,this->mode,this->is_open );
    }
};

struct task {
    task_type t_type;
    int64_t task_id=0;
    int8_t need_solving;
    explicit task(task_type t_type):t_type(t_type),need_solving(1){}
    task(const task &t_other):t_type(t_other.t_type),task_id(t_other.task_id)
            ,need_solving(t_other.need_solving){}
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( this->t_type,this->task_id,this->need_solving );
    }
};
struct write_task:public task{
    write_task():task(task_type::WRITE_TASK){}
    write_task(file source, file destination)
            :task(task_type::WRITE_TASK),source(source),destination(destination){}
    write_task(const write_task &write_task_t)
            :task(task_type::WRITE_TASK), source(write_task_t.source),
                                        destination(write_task_t.destination){}
    file source;
    file destination;
    bool meta_updated = false;

    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( this->t_type,this->task_id,this->need_solving,this->source,
                 this->destination,this->meta_updated);
    }
};
struct read_task:public task{
    file source;
    file destination;
    bool meta_updated = false;
    bool local_copy = false;
    read_task(const file &source, const file &destination)
            :task(task_type::READ_TASK),source(source),destination(destination){}
    read_task():task(task_type::READ_TASK){}
    read_task(const read_task &read_task_t)
            :task(task_type::READ_TASK), source(read_task_t.source),
                                        destination(read_task_t.destination){}

    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( this->t_type,this->task_id,this->need_solving,this->source,
                 this->destination,this->meta_updated,this->local_copy);
    }
};

struct flush_task:public task{
    flush_task():task(task_type::FLUSH_TASK){}
    flush_task(const flush_task &flush_task_t):task(task_type::FLUSH_TASK),
            source(flush_task_t.source),
            destination(flush_task_t.destination){}
    flush_task(file source,
            file destination,
            source_type dest_t,
            std::string datasource_id):task(task_type::FLUSH_TASK),source(source),destination(destination){}

    file source;
    file destination;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive(this->t_type,this->task_id,this->need_solving,this->source,this->destination);
    }
};

struct delete_task:public task{
    delete_task():task(task_type::DELETE_TASK){}
    delete_task(const delete_task &delete_task_i):task(task_type::DELETE_TASK),source(delete_task_i.source){}
    delete_task(file source):task(task_type::DELETE_TASK),source(source){}
    file source;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive(this->t_type,this->task_id,this->need_solving,this->source);
    }
};

struct solver_input_dp{
    int *worker_score;
    int num_task;
    int64_t *task_size;
    int64_t *worker_capacity;
    int *worker_energy;
    solver_input_dp(int num_task,int num_workers){
        this->num_task = num_task;
        worker_score=new int[num_workers];
        worker_capacity=new int64_t[num_workers];
        task_size=new int64_t[num_task];
        worker_energy=new int[num_workers];
    }

    virtual ~solver_input_dp() = default;
};
struct solver_output_dp{
    int max_value=0;
    int* solution;
    //workerID->list of taks
    std::unordered_map<int,std::vector<task*>> worker_task_map;

    explicit solver_output_dp(int num_task):worker_task_map(){
        solution=new int[num_task];
    }

    virtual ~solver_output_dp() = default;
};

struct solver_input{
    int num_task;
    int64_t *task_size;

    solver_input(int num_task,int num_workers){
        this->num_task = num_task;
        task_size=new int64_t[num_task];
    }

    virtual ~solver_input() {}
};
struct solver_output{
    int* solution;
    //workerID->list of taks
    std::unordered_map<int,std::vector<task*>> worker_task_map;

    explicit solver_output(int num_task):worker_task_map(){
        solution=new int[num_task];
    }

    virtual ~solver_output() {}
};


#endif //AETRIO_MAIN_STRUCTURE_H
