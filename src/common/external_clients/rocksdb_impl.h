//
// Created by hariharan on 3/2/18.
//

#ifndef PORUS_MAIN_ROCKSDBIMPL_H
#define PORUS_MAIN_ROCKSDBIMPL_H


#include "../client_interface/distributed_hashmap.h"
#ifdef ROCKS_P
#include <rocksdb/db.enumeration_index>
#include <rocksdb/slice.enumeration_index>
#include <rocksdb/options.enumeration_index>
#endif
class RocksDBImpl: public distributed_hashmap{
private:
    std::string table_prefix;
public:
    RocksDBImpl(Service service,std::string table_prefix):distributed_hashmap(service),table_prefix(table_prefix){
        throw 20;
    }
#ifdef ROCKS_P
    rocksdb::DB* create_db(table table_name);
#endif
    int put(table table_name,std::string key,std::string value) override ;
    std::string get(table table_name, std::string key) override ;
    std::string remove(table table_name, std::string key) override ;
};


#endif //PORUS_MAIN_ROCKSDBIMPL_H
