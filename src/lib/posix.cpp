/******************************************************************************
*include files
******************************************************************************/
#include <zconf.h>
#include "posix.h"
#include "../common/task_builder/task_builder.h"
/******************************************************************************
*Interface
******************************************************************************/
FILE *aetrio::fopen(const char *filename, const char *mode) {
    auto mdm = metadata_manager::getInstance(LIB);
    FILE* fh;
    if(!mdm->is_created(filename)){
        if(strcmp(mode,"r")==0
           || strcmp(mode,"weight")==0
           || strcmp(mode,"consider_after_a")==0){
            return nullptr;
        }else{
            mdm->create(filename,mode,fh);
        }
    }else{
        if(mdm->is_opened(filename))
            mdm->update_on_open(filename,mode,fh);
        else return nullptr;
    }
    return fh;
}

int aetrio::fclose(FILE *stream) {
    std::shared_ptr<metadata_manager> mdm=metadata_manager::getInstance(LIB);
    if(!mdm->is_opened(stream)) return -1;
    return mdm->update_on_close(stream);
}

int aetrio::fseek(FILE *stream, long int offset, int origin) {

    std::shared_ptr<metadata_manager> mdm=metadata_manager::getInstance(LIB);
    auto filename=mdm->get_filename(stream);
    if( mdm->get_mode(filename)=="consider_after_a" ||
            mdm->get_mode(filename)=="consider_after_a+") return 0;
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
            ->get_queue_client(CLIENT_TASK_SUBJECT);
    auto task_m = task_builder::getInstance(LIB);
    auto data_m = data_manager::getInstance(LIB);
    auto filename = mdm->get_filename(stream);
    auto offset = mdm->get_fp(filename);
    if(!mdm->is_opened(filename)) return 0;
    auto tasks = task_m->build_read_task
            (read_task(file(filename, offset, size * count), file()));
    int ptr_pos=0;

    for(auto task:tasks){
        char * data;
        switch(task.source.dest_t){
            case BUFFER_LOC:{
                client_queue->publish_task(&task);
                while(!data_m->exists(task.destination.filename)) usleep(20);
                data = const_cast<char *>(data_m->get(task.destination.filename).c_str());
                data_m->remove(DATASPACE_DB,task.destination.filename);
                break;
            }
            case DATASPACE_LOC:{
                data = const_cast<char *>(data_m->get(task.source.filename).c_str());
                break;
            }
        }
        memcpy(ptr+ptr_pos,data+task.source.offset,task.source.size);
    }
    mdm->update_read_task_info(tasks,filename);
    return size*count;
}

size_t aetrio::fwrite(void *ptr, size_t size, size_t count, FILE *stream) {
    auto mdm = metadata_manager::getInstance(LIB);
    auto client_queue = aetrio_system::getInstance(LIB)->get_queue_client
            (CLIENT_TASK_SUBJECT);
    auto task_m=task_builder::getInstance(LIB);
    auto data_m=data_manager::getInstance(LIB);
    auto filename=mdm->get_filename(stream);
    auto offset=mdm->get_fp(filename);
    if(!mdm->is_opened(filename)) return 0;
    auto tsk=write_task(file(filename,offset,size*count),file());
    auto write_tasks= task_m->build_write_task(tsk, static_cast<char *>(ptr));
    std::string id;
    int index=0;
    std::string data((char*)ptr);
    for(auto task:write_tasks){
        id=task.destination.filename;
        std::string temp_data=data.substr(task.source.offset,task.destination.size);
        data_m->put(id, temp_data);
        mdm->update_write_task_info(task,filename);
        client_queue->publish_task(&task);
        index++;
    }
    return size*count;
}


