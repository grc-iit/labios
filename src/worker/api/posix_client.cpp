//
// Created by hdevarajan on 5/10/18.
//

#include <iomanip>
#include "../../common/timer.h"
#include "posix_client.h"
#include "../../common/client_interface/distributed_hashmap.h"
#include "../../aetrio_system.h"


int posix_client::read(read_task task) {
#ifdef TIMERW
    Timer t=Timer();
    t.resumeTime();
#endif
    FILE* fh=fopen(task.source.filename.c_str(),"r+");
    auto data= static_cast<char *>(malloc(sizeof(char) * task.source.size));
    long long int pos=fseek(fh,task.source.offset,SEEK_SET);
    if(pos!=0)
        std::cerr << "posix_client::read() seek failed\n";
        //throw std::runtime_error("posix_client::read() seek failed");
    size_t count=fread(data,sizeof(char),task.source.size,fh);
    if(count!=task.source.size)
        std::cerr << "posix_client::read() read failed\n";
        //throw std::runtime_error("posix_client::read() read failed");

    auto map_client=aetrio_system::getInstance(WORKER)->map_client();
    serialization_manager sm=serialization_manager();
    map_client->put(DATASPACE_DB,task.destination.filename,data,
                    std::to_string(task.destination.server));
    //std::cout<<task.destination.filename<<","<<task.destination.server<<"\n";
    fclose(fh);
    free(data);
    if(task.local_copy){
        int file_id=static_cast<int>(duration_cast< milliseconds >(
                system_clock::now().time_since_epoch()
        ).count());
        std::string file_path=dir+std::to_string(file_id);
        FILE* fh1=fopen(file_path.c_str(),"w+");
        fwrite(data,task.source.size, sizeof(char),fh1);
        fclose(fh1);
        size_t chunk_index=(task.source.offset/ MAX_IO_UNIT);
        size_t base_offset=chunk_index*MAX_IO_UNIT+task.source.offset%MAX_IO_UNIT;
        chunk_meta chunk_meta1;
        chunk_meta1.actual_user_chunk=task.source;
        chunk_meta1.destination.location=BUFFERS;
        chunk_meta1.destination.filename=file_path;
        chunk_meta1.destination.offset=0;
        chunk_meta1.destination.size=task.source.size;
        chunk_meta1.destination.worker=worker_index;
        std::string chunk_str= sm.serialize_chunk(chunk_meta1);
        map_client->put(table::CHUNK_DB, task.source.filename +std::to_string
                (base_offset),chunk_str, std::to_string(0));
    }
#ifdef TIMERW
    std::stringstream stream;
    stream  << "posix_client::read(),"
            <<std::fixed<<std::setprecision(10)
            <<t.pauseTime()<<"\n";
    std::cout << stream.str();
#endif
    return 0;
}

int posix_client::write(write_task task) {
#ifdef TIMERW
    Timer t=Timer();
    t.resumeTime();
#endif
    std::shared_ptr<distributed_hashmap> map_client=aetrio_system::getInstance(WORKER)->map_client();
    serialization_manager sm=serialization_manager();
    auto source=task.source;
    size_t chunk_index=(source.offset/ MAX_IO_UNIT);
    size_t base_offset=chunk_index*MAX_IO_UNIT+source.offset%MAX_IO_UNIT;
    std::string chunk_str=map_client->get(table::CHUNK_DB, task.source
            .filename + std::to_string(chunk_index * MAX_IO_UNIT),
                                          std::to_string(0));
    chunk_meta chunk_meta1= sm.deserialize_chunk(chunk_str);
    std::string data=map_client->get(DATASPACE_DB,task.destination.filename, std::to_string(task.destination.server));
    std::string file_path;
    if(chunk_meta1.destination.location==location_type::CACHE){
        /*
         * New I/O
         */
        auto file_id=static_cast<int64_t>
        (std::chrono::duration_cast<std::chrono::microseconds>
                        (std::chrono::system_clock::now().time_since_epoch()).count());
        file_path=dir+std::to_string(file_id);
#ifdef DEBUG
        std::cout <<data.length()<<" chunk index:"<<chunk_index
                  <<" dataspaceId:"<<task.destination.filename
                  <<" created filename:"<<file_path
                  <<" clientId: " << task.destination.server<<"\n";
#endif
        FILE* fh=fopen(file_path.c_str(),"w+");
        auto count = fwrite(data.c_str(),sizeof(char),task.destination.size,fh);
        if(count!=task.destination.size) std::cerr<<"written less"<<count<<"\n";
        fclose(fh);
    }else{
        /*cd
         * existing I/O
         */
#ifdef DEBUG
        std::cout << "update file  "<<data.length()
                  <<" chunk index:"<<chunk_index
                  <<" dataspaceId:"<<task.destination.filename
                  <<" clientId: " << task.destination.server<<"\n";
#endif

        file_path=chunk_meta1.destination.filename;
        FILE* fh=fopen(chunk_meta1.destination.filename.c_str(),"r+");
        fseek(fh,task.source.offset-base_offset,SEEK_SET);
        fwrite(data.c_str(),sizeof(char),task.source.size,fh);
        fclose(fh);
    }
    chunk_meta1.actual_user_chunk=task.source;
    chunk_meta1.destination.location=BUFFERS;
    chunk_meta1.destination.filename=file_path;
    chunk_meta1.destination.offset=0;
    chunk_meta1.destination.size=task.destination.size;
    chunk_meta1.destination.worker=worker_index;
    chunk_str= sm.serialize_chunk(chunk_meta1);
    map_client->put(table::CHUNK_DB, task.source.filename +std::to_string
            (chunk_index * MAX_IO_UNIT),chunk_str, std::to_string(0));
//    map_client->remove(DATASPACE_DB,task.destination.filename, std::to_string(task.destination.server));
    map_client->put(table::WRITE_FINISHED_DB, task.destination.filename,
            std::to_string(task.destination.server), std::to_string(0));
#ifdef TIMERW
    std::stringstream stream;
    stream  << "posix_client::write(),"
            <<std::fixed<<std::setprecision(10)
            <<t.pauseTime()<<"\n";
    std::cout << stream.str();
#endif
    return 0;
}

int posix_client::delete_file(delete_task task) {
    remove(task.source.filename.c_str());
    //TODO:update metadata of delete
    return 0;
}

int posix_client::flush_file(flush_task task) {
    FILE* fh_source=fopen(task.source.filename.c_str(),"rb+");
    FILE* fh_destination=fopen(task.destination.filename.c_str(),"rb+");
    char* data= static_cast<char *>(malloc(sizeof(char) * task.source.size));
    fseek(fh_source,task.source.offset,SEEK_SET);
    fread(data,task.source.size, sizeof(char),fh_source);
    fseek(fh_destination,task.destination.offset,SEEK_SET);
    fwrite(data,task.destination.size, sizeof(char),fh_destination);
    fclose(fh_destination);
    fclose(fh_source);
    return 0;
}
