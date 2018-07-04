//
// Created by hariharan on 3/2/18.
//

#ifdef ROCKS_P
#ifndef AETRIO_MAIN_ROCKSDBIMPL_H
#define AETRIO_MAIN_ROCKSDBIMPL_H


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
    RocksDBImpl(service service,std::string table_prefix)
            :distributed_hashmap(service),table_prefix(std::move(table_prefix)){
        throw 20;
    }
#ifdef ROCKS_P
    rocksdb::DB* create_db(const table &table_name);
#endif
    int put(const table &name,std::string key,const std::string &value) override ;
    std::string get(const table &name, std::string key) override ;
    std::string remove(const table &name, std::string key) override ;
};


#endif //AETRIO_MAIN_ROCKSDBIMPL_H
#else

#endif

