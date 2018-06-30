/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include <cstring>
#include <random>
#include "metadata_manager.h"
#include "../utilities.h"
#include "../return_codes.h"
std::shared_ptr<metadata_manager> metadata_manager::instance = nullptr;
/******************************************************************************
*Interface
******************************************************************************/
bool metadata_manager::is_created(std::string filename) {
    auto iter=file_map.find(filename);
    return iter!=file_map.end();
}

int metadata_manager::create(std::string filename, std::string mode, FILE* &fh) {
    if(filename.length() > FILENAME_MAX) return MDM__FILENAME_MAX_LENGTH;
    auto map=aetrio_system::getInstance(service_i)->map_client;
    fh=fmemopen(nullptr, 1, mode.c_str());
    file_stat stat ={fh,0,0,mode,true};
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) file_map.erase(iter);
    fh_map.emplace(fh,filename);
    file_map.emplace(filename,stat);
    std::string fs_str= serialization_manager().serialize_file_stat(stat);
    map->put(table::FILE_DB,filename,fs_str);
    /**
     * TODO: put in map for outstanding operations-> on fclose create a file
     * in the destination and flush buffer contents
    **/
    return SUCCESS;
}

bool metadata_manager::is_opened(std::string filename) {
    auto iter=file_map.find(filename);
    return iter!=file_map.end() && iter->second.is_open;
}

int metadata_manager::update_on_open(std::string filename,std::string mode,FILE * &fh) {
    if(filename.length() > FILENAME_MAX) return MDM__FILENAME_MAX_LENGTH;
    auto map = aetrio_system::getInstance(service_i)->map_client;
    auto iter = file_map.find(filename);
    file_stat stat;
    if(iter != file_map.end()){
        stat = iter->second;
        fh = stat.fh;
    }
    else{
        fh = fmemopen(nullptr, 1, mode.c_str());
        stat.file_size = 0;
        stat.fh = fh;
        stat.is_open = true;
        if(mode == "r" || mode == "r+"){
            stat.file_pointer = 0;
        }else if(mode == "w" || mode == "w+"){
            stat.file_pointer = 0;
            stat.file_size = 0;
            remove_chunks(filename);
        }else if(mode == "a" || mode =="a+"){
            stat.file_pointer = stat.file_size;
        }
    }
    iter=file_map.find(filename);
    if(iter!=file_map.end()) file_map.erase(iter);
    fh_map.emplace(fh,filename);
    file_map.emplace(filename,stat);
    std::string fs_str= serialization_manager().serialize_file_stat(stat);
    map->put(table::FILE_DB,filename,fs_str);
    return SUCCESS;
}

bool metadata_manager::is_opened(FILE *fh) {
    auto iter1=fh_map.find(fh);
    if(iter1!=fh_map.end()){
        auto iter=file_map.find(iter1->second);
        return iter!=file_map.end() && iter->second.is_open;
    }
    return false;
}

int metadata_manager::remove_chunks(std::string &filename) {
    auto map=aetrio_system::getInstance(service_i)->map_client;
    std::string chunks_str = map->remove(table::FILE_CHUNK_DB, filename);
    std::vector<std::string> chunks = string_split(chunks_str);
    for (const auto& chunk :chunks) {
        map->remove(table::CHUNK_DB, chunk);
    }
    return SUCCESS;
}

int metadata_manager::update_on_close(FILE *&fh) {
    auto iter=fh_map.find(fh);
    if(iter!=fh_map.end()) fh_map.erase(iter);
    return 0;
}

std::string metadata_manager::get_filename(FILE *fh) {
    auto iter=fh_map.find(fh);
    if(iter!=fh_map.end()) return iter->second;
    return nullptr;
}

std::size_t metadata_manager::get_filesize(std::string filename) {
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) iter->second.file_size;
    return 0;
}

std::string metadata_manager::get_mode(std::string filename) {
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) return iter->second.mode;
    return nullptr;
}

long long int metadata_manager::get_fp(const std::string &filename) {
    auto iter=file_map.find(filename);
    if(iter!=file_map.end())
        return static_cast<long long int>(iter->second.file_pointer);
    return -1;
}

int metadata_manager::update_read_task_info(std::vector<read_task> task_ks,std::string filename) {
    auto map = aetrio_system::getInstance(service_i)->map_client;
    file_stat fs;
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) fs=iter->second;
    std::string fs_str= serialization_manager().serialize_file_stat(fs);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}

int metadata_manager::update_write_task_info(std::vector<write_task> task_ks,std::string filename) {
    auto  map=aetrio_system::getInstance(service_i)->map_client;
    file_stat fs;
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) fs=iter->second;
    for(int i=0;i<task_ks.size();++i){
        auto task_k=task_ks[i];
        if(i==0){
            update_on_write(task_k.source.filename,task_k.source.size);
        }
        if(!task_k.meta_updated){
            auto base_offset=(task_k.destination.offset/MAX_IO_UNIT)
                             *MAX_IO_UNIT;
            chunk_meta cm;
            cm.actual_user_chunk=task_k.source;
            cm.destination=task_k.destination;
            std::string chunk_str= serialization_manager().serialize_chunk(cm);
            map->put(table::CHUNK_DB, filename+std::to_string(base_offset),chunk_str);
        }
    }
    std::string fs_str= serialization_manager().serialize_file_stat(fs);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}

int metadata_manager::update_on_seek(std::string filename,
                                     size_t offset, size_t origin){
    auto map=aetrio_system::getInstance(service_i)->map_client;
    serialization_manager sm=serialization_manager();
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()){
        switch(origin){
            case SEEK_SET:{
                if(offset <= iter->second.file_size && offset >=0) {
                    iter->second.file_pointer = offset;
                }
                break;
            }
            case SEEK_CUR:{
                if(iter->second.file_pointer + offset <= iter->second.file_size){
                    iter->second.file_pointer+=offset;
                }
                break;
            }
            case SEEK_END:{
                if(offset <= iter->second.file_size){
                    iter->second.file_pointer=iter->second.file_size-offset;
                }
                break;
            }
            default:
                std::cerr << "fseek origin error\n";
        }
        std::string fs_str= sm.serialize_file_stat(iter->second);
        map->put(table::FILE_DB,filename,fs_str);
    }
    return 0;
}

void metadata_manager::update_on_read(std::string filename, size_t size) {
    std::shared_ptr<distributed_hashmap>  map=aetrio_system::getInstance(service_i)->map_client;
    serialization_manager sm=serialization_manager();
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()){
        file_stat fs=iter->second;
        if(fs.file_size < fs.file_pointer+size){
            fs.file_size=fs.file_pointer+size;
        }
        fs.file_pointer+=size;
        std::string fs_str= sm.serialize_file_stat(fs);
        map->put(table::FILE_DB,filename,fs_str);
    }
}

void metadata_manager::update_on_write(std::string filename, size_t size) {
    auto map = aetrio_system::getInstance(service_i)->map_client;
    serialization_manager sm=serialization_manager();
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()){
        file_stat fs=iter->second;
        if(fs.file_size < fs.file_pointer+size){
            fs.file_size=fs.file_pointer+size;
        }
        fs.file_pointer+=size;
        std::string fs_str= sm.serialize_file_stat(fs);
        map->put(table::FILE_DB,filename,fs_str);
    }
}

std::vector<chunk_meta> metadata_manager::fetch_chunks(read_task task) {
    auto map = aetrio_system::getInstance(service_i)->map_client;
    auto base_offset = (task.source.offset / MAX_IO_UNIT) * MAX_IO_UNIT;
    auto remaining_data = task.source.size;
    auto chunks = std::vector<chunk_meta>();
    chunk_meta cm;
    while(remaining_data>0){
        auto chunk_str = map->get(table::CHUNK_DB,
                                  task.source.filename+std::to_string(base_offset));
        if(!chunk_str.empty()){
            cm = serialization_manager().deserialize_chunk(chunk_str);
        }else{
            cm.actual_user_chunk = task.source;
            cm.destination.dest_t = source_type::PFS_LOC;
            cm.destination.size = cm.actual_user_chunk.size;
            cm.destination.filename = cm.actual_user_chunk.filename;
            cm.destination.offset = cm.actual_user_chunk.offset;
            std::default_random_engine generator;
            std::uniform_int_distribution<int> dist(1, MAX_WORKER_COUNT);
            cm.destination.worker = dist(generator);
        }
        chunks.push_back(cm);
        if(remaining_data>=cm.actual_user_chunk.size)
            remaining_data -= cm.actual_user_chunk.size;
        else remaining_data=0;
        base_offset += cm.actual_user_chunk.size;
    }
    return chunks;
}

int metadata_manager::update_write_task_info(write_task task_k, std::string filename) {
    auto map=aetrio_system::getInstance(service_i)->map_client;
    file_stat fs;
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) fs=iter->second;
    update_on_write(task_k.source.filename,task_k.source.size);
    if(!task_k.meta_updated){
        auto chunk_index=(task_k.source.offset/ MAX_IO_UNIT);
        auto base_offset=chunk_index*MAX_IO_UNIT+
                         task_k.source.offset%MAX_IO_UNIT;
        chunk_meta cm;
        cm.actual_user_chunk=task_k.source;
        cm.destination=task_k.destination;
        std::string chunk_str= serialization_manager().serialize_chunk(cm);
        map->put(table::CHUNK_DB, filename+std::to_string(base_offset),chunk_str);
    }
    std::string fs_str= serialization_manager().serialize_file_stat(fs);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}

