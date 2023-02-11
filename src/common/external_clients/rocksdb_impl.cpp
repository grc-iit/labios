/*
 * Copyright (C) 2019  SCS Lab <scs-help@cs.iit.edu>, Hariharan
 * Devarajan <hdevarajan@hawk.iit.edu>, Anthony Kougkas
 * <akougkas@iit.edu>, Xian-He Sun <sun@iit.edu>
 *
 * This file is part of Labios
 *
 * Labios is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
//
// Created by hariharan on 3/2/18.
//

#include <labios/common/external_clients/rocksdb_impl.h>

int RocksDBImpl::put(const table &name, std::string key,
                     const std::string &value, std::string group_key) {

#ifdef ROCKS_P
  rocksdb::DB *db = create_db(name);
  rocksdb::Status s = db->Put(rocksdb::WriteOptions(), key, value);
  return s.ok();
#else
  throw 20;
#endif
}

std::string RocksDBImpl::get(const table &name, std::string key,
                             std::string group_key) {
#ifdef ROCKS_P
  rocksdb::DB *db = create_db(name);
  std::string value = std::string();
  rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
  return value;
#else
  throw 20;
#endif
}

std::string RocksDBImpl::remove(const table &name, std::string key,
                                std::string group_key) {
#ifdef ROCKS_P
  rocksdb::DB *db = create_db(name);
  std::string value = std::string();
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
rocksdb::DB *RocksDBImpl::create_db(const table &table_name) {
  rocksdb::DB *db;
  rocksdb::Options options;
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options.IncreaseParallelism();
  // options.OptimizeLevelStyleCompaction();
  //  create the DB if it's not already present
  options.create_if_missing = true;
  // open DB
  rocksdb::Status s = rocksdb::DB::Open(
      options, table_prefix + std::to_string(table_name), &db);
  return db;
}
#endif
