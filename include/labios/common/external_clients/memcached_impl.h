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
 * Created by hariharan on 2/3/18.
 * Updated by akougkas on 6/26/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_MEMCACHEDIMPL_H
#define LABIOS_MAIN_MEMCACHEDIMPL_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <city.h>
#include <cstring>
#include <labios/common/client_interface/distributed_hashmap.h>
#include <libmemcached/memcached.h>
/******************************************************************************
 *Class
 ******************************************************************************/
class MemcacheDImpl : public distributed_hashmap {
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
private:
  memcached_st *mem_client;
  size_t num_servers;
  std::string get_server(std::string key);

public:
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  MemcacheDImpl(service service, const std::string &config_string, int server)
      : distributed_hashmap(service) {
    mem_client = memcached(config_string.c_str(), config_string.size());
    num_servers = mem_client->number_of_hosts;
  }
  size_t get_servers() override { return num_servers; }
  /******************************************************************************
   *Interface
   ******************************************************************************/
  int put(const table &name, std::string key, const std::string &value,
          std::string group_key) override;
  std::string get(const table &name, std::string key,
                  std::string group_key) override;
  std::string remove(const table &name, std::string key,
                     std::string group_key) override;
  bool exists(const table &name, std::string key,
              std::string group_key) override;
  bool purge() override;

  size_t counter_init(const table &name, std::string key,
                      std::string group_key) override;

  size_t counter_inc(const table &name, std::string key,
                     std::string group_key) override;

  /******************************************************************************
   *Destructor
   ******************************************************************************/
  virtual ~MemcacheDImpl() {}
};

#endif // LABIOS_MAIN_MEMCACHEDIMPL_H
