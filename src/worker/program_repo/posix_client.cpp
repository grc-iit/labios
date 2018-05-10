//
// Created by hdevarajan on 5/10/18.
//

#include "posix_client.h"
#include "../../common/client_interface/distributed_hashmap.h"
#include "../../porus_system.h"

int PosixClient::read(read_task task) {
    FILE* fh=fopen(task.source.filename.c_str(),"r+");
    char* data= static_cast<char *>(malloc(sizeof(char) * task.source.size));
    fseek(fh,task.source.offset,SEEK_SET);
    fread(data,task.source.size, sizeof(char),fh);
    std::shared_ptr<distributed_hashmap> map=porus_system::getInstance(WORKER)->map_client;
    map->put(DATASPACE_DB,task.destination.filename,data);
    //TODO:update MedataManager of read
    fclose(fh);
    return 0;
}

int PosixClient::write(write_task task) {
    std::shared_ptr<distributed_hashmap> map=porus_system::getInstance(WORKER)->map_client;
    std::string data=map->get(DATASPACE_DB,task.source.filename);
    FILE* fh=fopen(task.destination.filename.c_str(),"r+");
    fseek(fh,task.destination.offset,SEEK_SET);
    fwrite(data.c_str(),task.destination.size, sizeof(char),fh);
    //TODO:update MedataManager of write
    fclose(fh);
    return 0;
}

int PosixClient::delete_file(read_task task) {
    remove(task.source.filename.c_str());
    //TODO:update metadata of delete
    return 0;
}

int PosixClient::flush_file(read_task task) {
    FILE* fh_source=fopen(task.source.filename.c_str(),"r+");
    FILE* fh_destination=fopen(task.destination.filename.c_str(),"r+");
    char* data= static_cast<char *>(malloc(sizeof(char) * task.source.size));
    fseek(fh_source,task.source.offset,SEEK_SET);
    fread(data,task.source.size, sizeof(char),fh_source);
    fseek(fh_destination,task.destination.offset,SEEK_SET);
    fwrite(data,task.destination.size, sizeof(char),fh_destination);
    fclose(fh_destination);
    fclose(fh_source);
    return 0;
}
