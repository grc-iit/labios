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
/******************************************************************************
 *include files
 ******************************************************************************/
#include <cmath>
#include <labios/common/metadata_manager/metadata_manager.h>
#include <labios/common/task_builder/task_builder.h>
#include <vector>

std::shared_ptr<task_builder> task_builder::instance = nullptr;
/******************************************************************************
 *Interface
 ******************************************************************************/
std::vector<write_task *> task_builder::build_write_task(write_task task,
                                                         std::string data) {
  auto map_client = labios_system::getInstance(service_i)->map_client();
  auto map_server = labios_system::getInstance(service_i)->map_server();
  auto tasks = std::vector<write_task *>();
  file source = task.source;

  auto number_of_tasks =
      static_cast<int>(std::ceil((float)(source.size) / MAX_IO_UNIT));
  auto dataspace_id =
      map_server->counter_inc(COUNTER_DB, DATASPACE_ID, std::to_string(-1));

  std::size_t base_offset =
      (source.offset / MAX_IO_UNIT) * MAX_IO_UNIT + source.offset % MAX_IO_UNIT;
  std::size_t data_offset = 0;
  std::size_t remaining_data = source.size;
  int server = static_cast<int>(labios_system::getInstance(LIB)->rank /
                                PROCS_PER_MEMCACHED);
  // std::cout<<"rank"<<labios_system::getInstance(LIB)->rank<<"server:"<<server
  // <<"\n";
  while (remaining_data > 0) {
    std::size_t chunk_index = base_offset / MAX_IO_UNIT;
    auto sub_task = new write_task(task);
    sub_task->task_id = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    /****************** write not aligned to 2 MB offsets
     * ************************/
    if (base_offset != chunk_index * MAX_IO_UNIT) {
      size_t bucket_offset = base_offset - chunk_index * MAX_IO_UNIT;
      std::string chunk_str = map_client->get(
          table::CHUNK_DB,
          source.filename + std::to_string(chunk_index * MAX_IO_UNIT),
          std::to_string(-1));
      chunk_meta cm = serialization_manager().deserialize_chunk(chunk_str);
      /*************************** chunk in dataspace
       * ******************************/
      if (cm.destination.location == location_type::CACHE) {
        /********* update new data in dataspace **********/
        auto chunk_value =
            map_client->get(table::DATASPACE_DB, cm.destination.filename,
                            std::to_string(cm.destination.server));
        std::size_t size_to_write = 0;
        if (remaining_data < MAX_IO_UNIT) {
          size_to_write = remaining_data;
        } else {
          size_to_write = MAX_IO_UNIT;
        }
        if (remaining_data + bucket_offset > MAX_IO_UNIT) {
          size_to_write = MAX_IO_UNIT - bucket_offset;
        }
        if (chunk_value.length() >= bucket_offset + size_to_write) {
          chunk_value.replace(bucket_offset, size_to_write,
                              data.substr(data_offset, bucket_offset));
        } else {
          chunk_value = chunk_value.substr(0, bucket_offset) +
                        data.substr(data_offset, size_to_write);
        }
        map_client->put(table::DATASPACE_DB, cm.destination.filename,
                        chunk_value, std::to_string(cm.destination.server));
        sub_task->addDataspace = false;
        if (chunk_value.length() == MAX_IO_UNIT) {
          sub_task->publish = true;
          sub_task->destination.size = MAX_IO_UNIT;
          sub_task->destination.offset = 0;
          sub_task->source.offset = base_offset;
          sub_task->source.size = size_to_write;
          sub_task->source.filename = source.filename;
          sub_task->destination.location = location_type::CACHE;
          sub_task->destination.filename = cm.destination.filename;
          sub_task->destination.server = cm.destination.worker;
        } else {
          /********* build new task **********/
          sub_task->destination.size = chunk_value.length();
          sub_task->destination.offset = 0;
          sub_task->source.offset = chunk_index * MAX_IO_UNIT;
          sub_task->source.size = chunk_value.length();
          sub_task->source.filename = source.filename;
          sub_task->destination.location = location_type::CACHE;
          sub_task->destination.filename = cm.destination.filename;
          sub_task->destination.server = cm.destination.worker;
        }

      } else {
        /****************************** chunk in file
         * ********************************/
        std::size_t size_to_write = 0;
        if (remaining_data < MAX_IO_UNIT) {
          size_to_write = remaining_data;
        } else {
          size_to_write = MAX_IO_UNIT;
          sub_task->publish = true;
        }
        if (remaining_data + bucket_offset > MAX_IO_UNIT) {
          size_to_write = MAX_IO_UNIT - bucket_offset;
        }
        sub_task->destination.worker = cm.destination.worker;
        sub_task->destination.size = size_to_write;
        sub_task->destination.offset = bucket_offset;
        sub_task->source.offset = base_offset;
        sub_task->source.size = size_to_write;
        sub_task->source.filename = source.filename;
        sub_task->destination.location = location_type::CACHE;
        sub_task->destination.server = server;
        sub_task->destination.filename =
            std::to_string(dataspace_id) + "_" + std::to_string(chunk_index);
        sub_task->meta_updated = true;
      }
      std::size_t size_to_write = 0;
      if (remaining_data < MAX_IO_UNIT) {
        size_to_write = remaining_data;
      } else {
        size_to_write = MAX_IO_UNIT;
      }
      if (remaining_data + bucket_offset > MAX_IO_UNIT) {
        size_to_write = MAX_IO_UNIT - bucket_offset;
      }
      base_offset += size_to_write;
      data_offset += size_to_write;
      remaining_data -= size_to_write;
    }
    /******************** write aligned to 2 MB offsets
       **************************/
    else {
      size_t bucket_offset = 0;
      /******* remaining_data I/O is less than 2 MB *******/
      if (remaining_data < MAX_IO_UNIT) {
        if (!map_client->exists(table::CHUNK_DB,
                                source.filename +
                                    std::to_string(chunk_index * MAX_IO_UNIT),
                                std::to_string(-1))) {
          sub_task->publish = false;
          sub_task->destination.size = remaining_data;
          sub_task->destination.offset = bucket_offset;
          sub_task->source.offset = base_offset;
          sub_task->source.size = sub_task->destination.size;
          sub_task->source.filename = source.filename;
          sub_task->destination.location = location_type::CACHE;
          sub_task->destination.server = server;
          sub_task->destination.filename =
              std::to_string(dataspace_id) + "_" + std::to_string(chunk_index);
        } else {
          std::string chunk_str = map_client->get(
              table::CHUNK_DB,
              source.filename + std::to_string(chunk_index * MAX_IO_UNIT),
              std::to_string(-1));
          chunk_meta cm = serialization_manager().deserialize_chunk(chunk_str);
          /********* chunk in dataspace **********/
          if (cm.destination.location == location_type::CACHE) {
            // update new data in dataspace
            auto chunk_value =
                map_client->get(table::DATASPACE_DB, cm.destination.filename,
                                std::to_string(cm.destination.server));
            if (chunk_value.size() >= bucket_offset + remaining_data) {
              chunk_value.replace(bucket_offset, remaining_data,
                                  data.substr(data_offset, remaining_data));
            } else {
              chunk_value = chunk_value.substr(0, bucket_offset - 1) +
                            data.substr(data_offset, remaining_data);
            }
            map_client->put(table::DATASPACE_DB, cm.destination.filename,
                            chunk_value, std::to_string(cm.destination.server));
            // build new task
            sub_task->addDataspace = false;
            sub_task->publish = false;
            sub_task->destination.size = remaining_data; // cm
                                                         //  .destination
                                                         //  .size;
            sub_task->destination.offset = bucket_offset;
            sub_task->source.offset = base_offset;
            sub_task->source.size = remaining_data;
            sub_task->source.filename = source.filename;
            sub_task->destination.location = location_type::CACHE;
            sub_task->destination.server = cm.destination.server;
            sub_task->destination.filename = cm.destination.filename;
          }
          /************ chunk in file *************/
          else {
            sub_task->publish = false;
            sub_task->destination.worker = cm.destination.worker;
            sub_task->destination.size = remaining_data;
            sub_task->destination.offset = bucket_offset;
            sub_task->source.offset = base_offset;
            sub_task->source.size = remaining_data;
            sub_task->source.filename = source.filename;
            sub_task->destination.location = location_type::CACHE;
            sub_task->destination.server = server;
            sub_task->destination.filename = std::to_string(dataspace_id) +
                                             "_" + std::to_string(chunk_index);
            sub_task->meta_updated = true;
          }
        }

        base_offset += remaining_data;
        data_offset += remaining_data;
        remaining_data = 0;
      }
      /************** remaining_data is >= 2 MB ***********/
      else {
        sub_task->publish = true;
        sub_task->destination.size = MAX_IO_UNIT;
        sub_task->destination.offset = bucket_offset;
        sub_task->source.offset = base_offset;
        sub_task->source.size = MAX_IO_UNIT;
        sub_task->source.filename = source.filename;
        sub_task->destination.location = location_type::CACHE;
        sub_task->destination.server = server;
        sub_task->destination.filename =
            std::to_string(dataspace_id) + "_" + std::to_string(chunk_index);
        base_offset += MAX_IO_UNIT;
        data_offset += MAX_IO_UNIT;
        remaining_data -= MAX_IO_UNIT;
      }
    }
    tasks.emplace_back(sub_task);
  }
  return tasks;
}

std::vector<read_task> task_builder::build_read_task(read_task task) {
  auto tasks = std::vector<read_task>();
  auto mdm = metadata_manager::getInstance(LIB);
  auto map_server = labios_system::getInstance(service_i)->map_server();
  auto map_client = labios_system::getInstance(service_i)->map_client();
  auto chunks = mdm->fetch_chunks(task);
  size_t data_pointer = 0;
  int server = static_cast<int>(labios_system::getInstance(LIB)->rank /
                                PROCS_PER_MEMCACHED);
  for (auto chunk : chunks) {
    auto rt = new read_task();
    rt->task_id = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    rt->source = chunk.destination;
    rt->destination.offset = 0;
    rt->destination.size = rt->source.size;
    rt->destination.server = server;
    data_pointer += rt->destination.size;
    rt->destination.filename = std::to_string(
        map_server->counter_inc(COUNTER_DB, DATASPACE_ID, std::to_string(-1)));
    tasks.push_back(*rt);
    delete (rt);
  }
  return tasks;
}
