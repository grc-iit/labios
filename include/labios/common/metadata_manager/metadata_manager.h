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
 * Created by hariharan on 2/16/18.
 * Updated by akougkas on 6/26/2018
 ******************************************************************************/
#ifndef LABIOS_MAIN_METADATA_MANAGER_H
#define LABIOS_MAIN_METADATA_MANAGER_H
/******************************************************************************
 *include files
 ******************************************************************************/
#include <cereal/types/memory.hpp>
#include <cstdio>
#include <labios/common/data_structures.h>
#include <labios/labios_system.h>
#include <string>
#include <unordered_map>
/******************************************************************************
 *Class
 ******************************************************************************/
class metadata_manager {
private:
  /******************************************************************************
   *Variables and members
   ******************************************************************************/
  static std::shared_ptr<metadata_manager> instance;
  std::unordered_map<FILE *, std::string> fh_map;
  std::unordered_map<std::string, file_stat> file_map;
  service service_i;
  /******************************************************************************
   *Constructor
   ******************************************************************************/
  explicit metadata_manager(service service)
      : fh_map(), file_map(), service_i(service) {}

public:
  /******************************************************************************
   *Interface
   ******************************************************************************/
  inline static std::shared_ptr<metadata_manager> getInstance(service service) {
    return instance == nullptr ? instance = std::shared_ptr<metadata_manager>(
                                     new metadata_manager(service))
                               : instance;
  }
  bool is_created(std::string filename);
  int create(std::string filename, std::string mode, FILE *&fh);
  bool is_opened(std::string filename);
  bool is_opened(FILE *fh);
  int update_on_open(std::string filename, std::string mode, FILE *&fh);
  int update_on_close(FILE *&fh);
  int remove_chunks(std::string &basic_string);
  std::string get_filename(FILE *fh);
  std::size_t get_filesize(std::string basic_string);
  std::string get_mode(std::string basic_string);
  long long int get_fp(const std::string &basic_string);
  int update_on_seek(std::string basic_string, size_t offset, size_t origin);
  int update_read_task_info(std::vector<read_task> task_k,
                            std::string filename);
  int update_write_task_info(std::vector<write_task> task_ks,
                             std::string filename);
  int update_write_task_info(write_task task_ks, std::string filename,
                             std::size_t io_size);
  std::vector<chunk_meta> fetch_chunks(read_task task);
  void update_on_read(std::string filename, size_t size);
  void update_on_write(std::string filename, size_t size, size_t offset);
  /******************************************************************************
   *Destructor
   ******************************************************************************/
  virtual ~metadata_manager() {
    // TODO: serialize structures and send down to labios_meta_file
  }
};

#endif // LABIOS_MAIN_METADATA_MANAGER_H
