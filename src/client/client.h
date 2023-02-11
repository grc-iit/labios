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
// Created by hariharan on 2/16/18.
//

#ifndef LABIOS_MAIN_CLIENT_H
#define LABIOS_MAIN_CLIENT_H

#include <labios/common/data_structures.h>

#include <future>
#include <mpi.h>
#include <unordered_map>

class LabiosClient {
private:
  std::unordered_map<size_t, MPI_Comm> application_map;
  std::unordered_map<std::string, file_meta> files;
  std::unordered_map<size_t, dataspace> dataspaces;
  int rank;
  size_t count;
  MPI_Comm applications_comms, client_comms;
  std::future<int> async_handle;
  LabiosClient() : count(0), application_map() {}

public:
  int init();
  int listen_application_connections();
  int initialize_application(size_t application_id);
  int listen_request();
  int update_file(file_meta f, std::string key);
  int update_chunk(file_meta f, std::string key);
  int update_dataspace(size_t id, dataspace data);
  int get_file(file_meta &f, std::string key);
  int delete_file(file_meta &f, std::string key);
  int get_dataspace(size_t id, dataspace &data);
  int get_chunk(file_meta &f, std::string key);
  int delete_chunk(file_meta &f, std::string key);
};

#endif // LABIOS_MAIN_CLIENT_H
