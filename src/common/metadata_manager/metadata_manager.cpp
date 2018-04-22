//
// Created by hariharan on 2/16/18.
//

#include <mpi.h>
#include <cstring>
#include "metadata_manager.h"
#include "../external_clients/serialization_manager.h"
#include "../utility.h"

std::shared_ptr<metadata_manager> metadata_manager::instance = nullptr;

bool metadata_manager::is_created(std::string filename) {
    auto iter=file_map.find(filename);
    return iter!=file_map.end();
}

int metadata_manager::create(std::string filename, std::string mode, FILE *&fh) {
    DistributedHashMap* map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    if(filename.length() > FILENAME_MAX)
        return -1;
    file_stat stat;
    stat.file_size=0;
    fh=fmemopen(NULL, 1, mode.c_str());
    stat.fh=fh;
    stat.file_pointer=0;
    stat.is_open=true;
    stat.mode=mode;
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) file_map.erase(iter);
    fh_map.emplace(fh,filename);
    file_map.emplace(filename,stat);
    std::string fs_str=sm.serialise_file_stat(stat);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}

bool metadata_manager::is_opened(std::string filename) {
    auto iter=file_map.find(filename);
    return iter!=file_map.end() && iter->second.is_open;
}

int metadata_manager::update_on_open(std::string filename, std::string mode, FILE * &fh) {
    if(filename.length() > FILENAME_MAX)
        return -1;

   DistributedHashMap*  map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    auto iter=file_map.find(filename);
    file_stat stat;
    stat.file_size=0;
    if(iter!=file_map.end()) stat=iter->second;
    fh=fmemopen(NULL, 1, mode.c_str());
    stat.fh=fh;
    stat.is_open=true;
    if(mode =="r" || mode=="r+"){
        stat.file_pointer=0;
    }else if(mode =="w" || mode=="w+"){
        stat.file_pointer=0;
        stat.file_size=0;
        remove_chunks(filename);
    }else if(mode =="a" || mode=="a+"){
        stat.file_pointer=stat.file_size;
    }
    iter=file_map.find(filename);
    if(iter!=file_map.end()) file_map.erase(iter);
    fh_map.emplace(fh,filename);
    file_map.emplace(filename,stat);
    std::string fs_str=sm.serialise_file_stat(stat);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}
bool metadata_manager::is_opened(FILE *fh) {
    auto iter1=fh_map.find(fh);
    if(iter1!=fh_map.end()){
        auto iter=file_map.find(iter1->second);
        return iter!=file_map.end() && iter->second.is_open;
    }
    return false;

}

int metadata_manager::remove_chunks(std::string filename) {
   DistributedHashMap*  map=System::getInstance(service)->map_client;
    std::string chunks_str = map->remove(table::FILE_CHUNK_DB, filename);
    std::vector<std::string> chunks = string_split(chunks_str);
    for (auto chunk :chunks) {
        map->remove(table::CHUNK_DB, chunk);
    }
}

int metadata_manager::update_on_close(FILE *&fh) {
    auto iter=fh_map.find(fh);
    if(iter!=fh_map.end()) fh_map.erase(iter);
    return 0;
}

std::string metadata_manager::get_filename(FILE *fh) {
    auto iter=fh_map.find(fh);
    if(iter!=fh_map.end()) return iter->second;
    return NULL;
}

std::size_t metadata_manager::get_filesize(std::string filename) {
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) iter->second.file_size;
    return 0;
}

std::string metadata_manager::get_mode(std::string filename) {
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) return iter->second.mode;
    return NULL;
}

std::size_t metadata_manager::get_fp(std::string filename) {
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) return iter->second.file_pointer;
    return -1;
}

int metadata_manager::update_read_task_info(std::vector<read_task> task_ks,std::string filename) {
    DistributedHashMap*  map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    file_stat fs;
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) fs=iter->second;
    std::string fs_str=sm.serialise_file_stat(fs);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}
int metadata_manager::update_write_task_info(std::vector<write_task> task_ks,std::string filename) {
    DistributedHashMap*  map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    file_stat fs;
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()) fs=iter->second;
    for(int i=0;i<task_ks.size();++i){
        auto task_k=task_ks[i];
        if(i==0){
            update_on_write(task_k.source.filename,task_k.source.size);
        }
        if(!task_k.meta_updated){
            size_t base_offset=(task_k.destination.offset/io_unit_max)*io_unit_max;
            chunk_meta cm=chunk_meta();
            cm.actual_user_chunk=task_k.source;
            cm.destination=task_k.destination;
            std::string chunk_str=sm.serialise_chunk(cm);
            map->put(table::CHUNK_DB, filename+std::to_string(base_offset),chunk_str);
        }
    }
    std::string fs_str=sm.serialise_file_stat(fs);
    map->put(table::FILE_DB,filename,fs_str);
    return 0;
}
int metadata_manager::update_on_seek(std::string filename,size_t offset, size_t origin){
   DistributedHashMap*  map=System::getInstance(service)->map_client;
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
            std::string fs_str=sm.serialise_file_stat(iter->second);
            map->put(table::FILE_DB,filename,fs_str);
        }
    }
    return 0;
}

void metadata_manager::update_on_read(std::string filename, size_t size) {
   DistributedHashMap*  map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()){
        file_stat fs=iter->second;
        if(fs.file_size < fs.file_pointer+size){
            fs.file_size=fs.file_pointer+size;
        }
        fs.file_pointer+=size;
        std::string fs_str=sm.serialise_file_stat(fs);
        map->put(table::FILE_DB,filename,fs_str);
    }
}

void metadata_manager::update_on_write(std::string filename, size_t size) {
   DistributedHashMap*  map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    auto iter=file_map.find(filename);
    if(iter!=file_map.end()){
        file_stat fs=iter->second;
        if(fs.file_size < fs.file_pointer+size){
            fs.file_size=fs.file_pointer+size;
        }
        fs.file_pointer+=size;
        std::string fs_str=sm.serialise_file_stat(fs);
        map->put(table::FILE_DB,filename,fs_str);
    }
}

std::vector<chunk_meta> metadata_manager::fetch_chunks(read_task task) {
    DistributedHashMap*  map=System::getInstance(service)->map_client;
    serialization_manager sm=serialization_manager();
    size_t base_offset=(task.source.offset/io_unit_max)*io_unit_max;
    size_t left=task.source.size;
    std::vector<chunk_meta> chunks=std::vector<chunk_meta>();
    while(left>0){
        std::string chunk_str=map->get(table::CHUNK_DB, task.source.filename+std::to_string(base_offset));
        chunk_meta cm=sm.deserialise_chunk(chunk_str);
        chunks.push_back(cm);
        left-=cm.actual_user_chunk.size;
        base_offset+=cm.actual_user_chunk.size;
    }
    return chunks;
}
