//
// Created by hariharan on 2/17/18.
//

#ifndef PORUS_MAIN_STRUCTURE_H
#define PORUS_MAIN_STRUCTURE_H



#include <cereal/types/memory.hpp>
#include "enumerations.h"
#include "constants.h"
#include <cereal/types/string.hpp>

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
    int64_t size;
    int worker=-1;
    file(std::string filename,
            long long int offset,
            long long int file_size):filename(filename),offset(offset),size(file_size),dest_t(DATASPACE_LOC){}
    file(const file &file_t):filename(file_t.filename),offset(file_t.offset),size(file_t.size),dest_t(file_t.dest_t),worker(file_t.worker){}

    file(){};
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
    size_t file_pointer;
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
    task_type t_type=DUMMY;
    uint64_t task_id;
    task(task_type t_type):t_type(t_type){}
    task(const task &t_other):t_type(t_other.t_type){}
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( this->t_type,this->task_id );
    }
};
struct write_task:public task{
    write_task():task(WRITE_TASK){}
    write_task(file source,
               file destination):task(WRITE_TASK),source(source),destination(destination){}
    write_task(const write_task &write_task_t):task(WRITE_TASK),
                                        source(write_task_t.source),
                                        destination(write_task_t.destination){}
    file source;
    file destination;
    bool meta_updated;

    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( cereal::base_class<task>( this ),this->source,this->destination,this->meta_updated);
    }
};
struct read_task:public task{
    read_task():task(READ_TASK){}
    read_task(const read_task &read_task_t):task(READ_TASK),
                                        source(read_task_t.source),
                                        destination(read_task_t.destination){}
    read_task(file source,
              file destination):task(WRITE_TASK),source(source),destination(destination){}

    bool meta_updated;
    file source;
    file destination;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( cereal::base_class<task>( this ),this->source,this->destination,this->meta_updated);
    }
};

struct flush_task:public task{
    flush_task():task(FLUSH_TASK){}
    flush_task(const flush_task &flush_task_t):task(FLUSH_TASK),
            source(flush_task_t.source),
            destination(flush_task_t.destination){}
    flush_task(file source,
            file destination,
            source_type dest_t,
            std::string datasource_id):task(FLUSH_TASK),source(source),destination(destination){}

    file source;
    file destination;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( cereal::base_class<task>( this ),this->source,this->destination);
    }
};

struct delete_task:public task{
    delete_task():task(DELETE_TASK){}
    delete_task(const delete_task &delete_task_i):task(DELETE_TASK),source(delete_task_i.source){}
    delete_task(file source):task(DELETE_TASK),source(source){}
    file source;
    template<class Archive>
    void serialize(Archive & archive)
    {
        archive( cereal::base_class<task>( this ),this->source);
    }
};

struct solver_input{
    int *worker_score;
    int num_task;
    int *task_size;
    int *worker_capacity;
    solver_input(int num_task,int num_workers){
        worker_score=new int[num_workers];
        worker_capacity=new int[num_workers];
        task_size=new int[num_task];
    }
    ~solver_input(){
        //delete(worker_score,worker_capacity,task_size);
    }
};
struct solver_output{
    int max_value;
    int* solution;
    std::unordered_map<int,std::vector<int>> worker_task_map;
    solver_output(int num_task):worker_task_map(){
        solution=new int[num_task];
    }
    ~solver_output(){
        delete(solution);
    }
};
#endif //PORUS_MAIN_STRUCTURE_H
