//
// Created by hariharan on 3/2/18.
//

#ifndef PORUS_MAIN_ROCKSDBIMPL_H
#define PORUS_MAIN_ROCKSDBIMPL_H


#include "DistributedHashMap.h"

#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <rocksdb/options.h>
class RocksDBImpl: public DistributedHashMap{
private:
    std::string table_prefix;
public:
    RocksDBImpl(Service service,std::string table_prefix):DistributedHashMap(service),table_prefix(table_prefix){

    }
    rocksdb::DB* create_db(table table_name);
    int put(table table_name,std::string key,std::string value) override ;
    std::string get(table table_name, std::string key) override ;
    std::string remove(table table_name, std::string key) override ;
};


#endif //PORUS_MAIN_ROCKSDBIMPL_H
