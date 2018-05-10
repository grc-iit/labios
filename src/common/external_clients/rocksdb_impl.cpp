//
// Created by hariharan on 3/2/18.
//

#include "rocksdb_impl.h"

int RocksDBImpl::put(table table1, std::string key, std::string value) {

#ifdef ROCKS_P
    rocksdb::DB* db=create_db(table1);
    rocksdb::Status s =db->Put(rocksdb::WriteOptions(), key, value);
    return s.ok();
#else
    throw 20;
#endif
}

std::string RocksDBImpl::get(table table1, std::string key) {
#ifdef ROCKS_P
    rocksdb::DB* db=create_db(table1);
    std::string value=std::string();
    rocksdb::Status s =db->Get(rocksdb::ReadOptions(), "key1", &value);
    return value;
#else
    throw 20;
#endif

}

std::string RocksDBImpl::remove(table table1, std::string key) {
#ifdef ROCKS_P
    rocksdb::DB* db=create_db(table1);
    std::string value=std::string();
    return value;
#else
    throw 20;
#endif

}
#ifdef ROCKS_P
rocksdb::DB* RocksDBImpl::create_db(table table_name) {
    rocksdb::DB* db;
    rocksdb::Options options;
    // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
    options.IncreaseParallelism();
    //options.OptimizeLevelStyleCompaction();
    // create the DB if it's not already present
    options.create_if_missing = true;
    // open DB
    rocksdb::Status s = rocksdb::DB::Open(options, table_prefix+std::to_string(table_name), &db);
    return 0;
}
#endif