/******************************************************************************
*include files
******************************************************************************/
#include <zconf.h>
#include "posix.h"
#include "../common/task_builder/task_builder.h"
#include "../common/return_codes.h"

/******************************************************************************
*Interface
******************************************************************************/
FILE* aetrio::fopen(const char *filename, const char *mode) {
    auto mdm = metadata_manager::getInstance(LIB);
    FILE* fh;
    if(!mdm->is_created(filename)){
        if(strcmp(mode,"r")==0 ||
           strcmp(mode,"w")==0 ||
           strcmp(mode,"a")==0){
            return nullptr;
        }else{
            if(mdm->create(filename,mode,fh)!= SUCCESS){
                throw std::runtime_error("aetrio::fopen() create failed!");
            }
        }
    }else{
        if(mdm->is_opened(filename)){
            if(mdm->update_on_open(filename,mode,fh) != SUCCESS){
                throw std::runtime_error("aetrio::fopen() update failed!");
            }
        }
        else return nullptr;
    }
    return fh;
}

int aetrio::fclose(FILE *stream) {
    auto mdm=metadata_manager::getInstance(LIB);
    if(!mdm->is_opened(stream)) return LIB__FCLOSE_FAILED;
    if(mdm->update_on_close(stream)!=SUCCESS) return LIB__FCLOSE_FAILED;
    return SUCCESS;
}

int aetrio::fseek(FILE *stream, long int offset, int origin) {
    auto mdm=metadata_manager::getInstance(LIB);
    auto filename=mdm->get_filename(stream);
    if( mdm->get_mode(filename)=="a" ||
        mdm->get_mode(filename)=="a+") return 0;
    auto size=mdm->get_filesize(filename);
    auto fp=mdm->get_fp(filename);
    switch(origin){
        case SEEK_SET:
            if(offset > size) return -1;
            break;
        case SEEK_CUR:
            if(fp + offset > size || fp + offset < 0) return -1;
            break;
        case SEEK_END:
            if(offset > 0) return -1;
            break;
        default:
            fprintf(stderr, "Seek origin fault!\n");
            return -1;
    }
    if(!mdm->is_opened(stream)) return -1;
    return mdm->update_on_seek(filename, static_cast<size_t>(offset),
                               static_cast<size_t>(origin));
}

size_t aetrio::fread(void *ptr, size_t size, size_t count, FILE *stream) {
    auto mdm = metadata_manager::getInstance(LIB);
    auto client_queue = aetrio_system::getInstance(LIB)
            ->get_client_queue(CLIENT_TASK_SUBJECT);
    auto task_m = task_builder::getInstance(LIB);
    auto data_m = data_manager::getInstance(LIB);
    auto filename = mdm->get_filename(stream);
    auto offset = mdm->get_fp(filename);
    if(!mdm->is_opened(filename)) return 0;
    auto r_task = read_task(file(filename, offset, size * count), file());
    auto tasks = task_m->build_read_task(r_task);
    int ptr_pos=0;

    for(auto task:tasks){
        char * data;
        switch(task.source.location){
            case PFS:
            case BUFFERS:{
                client_queue->publish_task(&task);
                while(!data_m->exists(DATASPACE_DB,task.destination.filename)){}
                data = const_cast<char *>(data_m->get(DATASPACE_DB,
                        task.destination.filename).c_str());
                data_m->remove(DATASPACE_DB,task.destination.filename);
                break;
            }
            case CACHE:{
                data = const_cast<char *>(data_m->get(DATASPACE_DB,
                        task.source.filename).c_str());
                break;
            }
        }
        memcpy(ptr+ptr_pos,data+task.destination.offset,task.destination.size);
        ptr_pos+=task.destination.size;
    }
    mdm->update_read_task_info(tasks,filename);
    return size*count;
}

size_t aetrio::fwrite(void *ptr, size_t size, size_t count, FILE *stream) {

    auto mdm = metadata_manager::getInstance(LIB);
    auto client_queue = aetrio_system::getInstance(LIB)->get_client_queue
            (CLIENT_TASK_SUBJECT);
    auto task_m = task_builder::getInstance(LIB);
    auto data_m = data_manager::getInstance(LIB);
    auto filename = mdm->get_filename(stream);
    auto offset = mdm->get_fp(filename);
    if(!mdm->is_opened(filename))
        throw std::runtime_error("aetrio::fwrite() file not opened!");


    auto w_task = write_task(file(filename,offset,size*count),file());


    auto write_tasks = task_m->build_write_task(w_task,static_cast<char*>(ptr));



    int index=0;
    std::string write_data(static_cast<char*>(ptr));
    for(auto task:write_tasks){
        if(task->addDataspace){
            if(write_data.length() >= task->source.offset + task->source.size){
                auto data=write_data.substr(task->source.offset,
                        task->source.size);
                data_m->put(DATASPACE_DB, task->destination.filename, data);
            }else{
                data_m->put(DATASPACE_DB, task->destination.filename, write_data);
            }

        }

        if(task->publish){
            if(size*count < task->source.size)
                mdm->update_write_task_info(*task,filename,size*count);
            else
                mdm->update_write_task_info(*task,filename,task->source.size);
            client_queue->publish_task(task);
        }else{
            mdm->update_write_task_info(*task,filename,task->source.size);
        }

        index++;
        delete task;
    }
    return size*count;
}


