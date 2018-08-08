//
// Created by hariharan on 3/2/18.
//

#include "rocksdb_impl.h"

int RocksDBImpl::put(const table &name, std::string key, const std::string &value,std::string group_key) {

#ifdef ROCKS_P
    rocksdb::DB* db=create_db(name);
    rocksdb::Status s =db->Put(rocksdb::WriteOptions(), key, value);
    return s.ok();
#else
    throw 20;
#endif
}

std::string RocksDBImpl::get(const table &name, std::string key,std::string group_key) {
#ifdef ROCKS_P
    rocksdb::DB* db=create_db(name);
    std::string value=std::string();
    rocksdb::Status s =db->Get(rocksdb::ReadOptions(), key, &value);
    return value;
#else
    throw 20;
#endif

}

std::string RocksDBImpl::remove(const table &name, std::string key,std::string group_key) {
#ifdef ROCKS_P
    rocksdb::DB* db=create_db(name);
    std::string value=std::string();
    return value;
#else
    throw 20;
#endif

}

size_t RocksDBImpl::counter_init(const table &name, std::string key,
                                 std::string group_key) {
    return distributed_hashmap::counter_init(name, key, group_key);
}

size_t RocksDBImpl::counter_inc(const table &name, std::string key,
                                std::string group_key) {
    return distributed_hashmap::counter_inc(name, key, group_key);
}

#ifdef ROCKS_P
rocksdb::DB* RocksDBImpl::create_db(const table &table_name) {
    rocksdb::DB* db;
    rocksdb::Options options;
    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    options.IncreaseParallelism();
    //options.OptimizeLevelStyleCompaction();
    // create the DB if it's not already present
    options.create_if_missing = true;
    // open DB
    rocksdb::Status s = rocksdb::DB::Open(options, table_prefix+std::to_string(table_name), &db);
    return db;
}
#endif