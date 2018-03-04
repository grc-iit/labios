//
// Created by hariharan on 2/16/18.
//

#ifndef PORUS_MAIN_PORUSCLIENT_H
#define PORUS_MAIN_PORUSCLIENT_H


#include <unordered_map>
#include <future>
#include "../common/structure.h"

class PorusClient {
private:
    std::unordered_map<size_t,MPI_Comm> application_map;
    std::unordered_map<std::string,file_meta> files;
    std::unordered_map<size_t,dataspace> dataspaces;
    int rank;
    size_t count;
    MPI_Comm applications_comms,client_comms;
    std::future<int> async_handle;
    PorusClient():count(0),application_map(){

    }
public:
    int init();
    int listen_application_connections();
    int initialize_application(size_t application_id);
    int listen_request();
    int update_file(file_meta f,std::string key);
    int update_chunk(file_meta f,std::string key);
    int update_dataspace(size_t id,dataspace data);
    int get_file(file_meta &f,std::string key);
    int delete_file(file_meta &f,std::string key);
    int get_dataspace(size_t id,dataspace &data);
    int get_chunk(file_meta &f,std::string key);
    int delete_chunk(file_meta &f,std::string key);



};


#endif //PORUS_MAIN_PORUSCLIENT_H
