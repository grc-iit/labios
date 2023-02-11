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
/*******************************************************************************
 * Created by hariharan on 3/2/18.
 * Updated by akougkas on 7/5/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_ROCKSDBIMPL_H
#define LABIOS_MAIN_ROCKSDBIMPL_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <labios/common/client_interface/distributed_hashmap.h>
#ifdef ROCKS_P
#include <rocksdb/db.enumeration_index>
#include <rocksdb/options.enumeration_index>
#include <rocksdb/slice.enumeration_index>
#endif
/******************************************************************************
 *Class
 ******************************************************************************/
class RocksDBImpl : public distributed_hashmap {
private:
  std::string table_prefix;

public:
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  RocksDBImpl(service service, std::string table_prefix)
      : distributed_hashmap(service), table_prefix(std::move(table_prefix)) {
    throw 20;
  }
#ifdef ROCKS_P
  rocksdb::DB *create_db(const table &table_name);
#endif
  /******************************************************************************
   *Interface
   ******************************************************************************/
  int put(const table &name, std::string key, const std::string &value,
          std::string group_key) override;
  std::string get(const table &name, std::string key,
                  std::string group_key) override;
  std::string remove(const table &name, std::string key,
                     std::string group_key) override;

  size_t counter_init(const table &name, std::string key,
                      std::string group_key) override;

  size_t counter_inc(const table &name, std::string key,
                     std::string group_key) override;
  size_t get_servers() override {
    throw NotImplementedException("get_servers");
  }

  /******************************************************************************
   *Destructor
   ******************************************************************************/
  virtual ~RocksDBImpl() {}
};
#endif // LABIOS_MAIN_ROCKSDBIMPL_H
