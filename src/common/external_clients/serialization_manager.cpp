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
// Created by hariharan on 2/23/18.
//

#include <labios/common/external_clients/serialization_manager.h>

std::string serialization_manager::serialize_file_stat(file_stat stat) {
  std::stringstream ss; // any stream can be used

  {
    cereal::JSONOutputArchive oarchive(ss); // Create an output archive
    oarchive(stat);                         // Write the data to the archive
  }
  return ss.str();
}

chunk_meta serialization_manager::deserialize_chunk(std::string chunk_str) {
  chunk_meta cm;
  {
    std::stringstream ss(chunk_str);
    cereal::JSONInputArchive iarchive(ss); // Create an input archive
    iarchive(cm);
  }
  return cm;
}

std::string serialization_manager::serialize_chunk(chunk_meta meta) {
  std::stringstream ss; // any stream can be used

  {
    cereal::JSONOutputArchive oarchive(ss); // Create an output archive
    oarchive(meta);                         // Write the data to the archive
  }
  std::string serialized_str = ss.str();
  return serialized_str;
}

std::string serialization_manager::serialize_task(task *task) {
  switch (task->t_type) {
  case task_type::WRITE_TASK: {
    auto *wt = reinterpret_cast<write_task *>(task);
    std::stringstream ss; // any stream can be used

    {
      cereal::JSONOutputArchive oarchive(ss); // Create an output archive
      oarchive(*wt);                          // Write the data to the archive
    }
    return ss.str();
  }
  case task_type::READ_TASK: {
    auto *rt = reinterpret_cast<read_task *>(task);
    std::stringstream ss; // any stream can be used

    {
      cereal::JSONOutputArchive oarchive(ss); // Create an output archive
      oarchive(*rt);                          // Write the data to the archive
    }
    return ss.str();
  }
  }
  return std::string();
}

task *serialization_manager::deserialize_task(std::string string) {
  task cm(task_type::DUMMY);
  {
    std::stringstream ss(string);
    cereal::JSONInputArchive iarchive(ss); // Create an input archive
    iarchive(cm);
    switch (cm.t_type) {
    case task_type::WRITE_TASK: {
      auto *wt = new write_task();
      std::stringstream ss(string);
      cereal::JSONInputArchive iarchive(ss);
      iarchive(*wt);
      return wt;
    }
    case task_type::READ_TASK: {
      auto *wt = new read_task();
      std::stringstream ss(string);
      cereal::JSONInputArchive iarchive(ss);
      iarchive(*wt);
      return wt;
    }
    }
  }
  return &cm;
}
