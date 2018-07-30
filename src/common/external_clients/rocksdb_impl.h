/*******************************************************************************
* Created by hariharan on 3/2/18.
* Updated by akougkas on 7/5/2018
* Illinois Institute of Technology - SCS Lab
* (C) 2018
******************************************************************************/
#ifndef AETRIO_MAIN_ROCKSDBIMPL_H
#define AETRIO_MAIN_ROCKSDBIMPL_H
/******************************************************************************
*include files
******************************************************************************/
#include "../client_interface/distributed_hashmap.h"
#ifdef ROCKS_P
#include <rocksdb/db.enumeration_index>
#include <rocksdb/slice.enumeration_index>
#include <rocksdb/options.enumeration_index>
#endif
/******************************************************************************
*Class
******************************************************************************/
class RocksDBImpl: public distributed_hashmap{
private:
    std::string table_prefix;
public:
/******************************************************************************
*Constructor
******************************************************************************/
    RocksDBImpl(service service,std::string table_prefix)
            :distributed_hashmap(service),table_prefix(std::move(table_prefix)){
        throw 20;
    }
#ifdef ROCKS_P
    rocksdb::DB* create_db(const table &table_name);
#endif
/******************************************************************************
*Interface
******************************************************************************/
    int put(const table &name,std::string key,const std::string &value,std::string group_key) override;
    std::string get(const table &name, std::string key,std::string group_key) override ;
    std::string remove(const table &name, std::string key,std::string group_key) override ;
/******************************************************************************
*Destructor
******************************************************************************/
    virtual ~RocksDBImpl(){}
};
#endif //AETRIO_MAIN_ROCKSDBIMPL_H
