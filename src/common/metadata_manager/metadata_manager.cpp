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
#include <cstring>
#include <labios/common/metadata_manager/metadata_manager.h>
#include <labios/common/return_codes.h>
#include <labios/common/timer.h>
#include <labios/common/utilities.h>
#include <mpi.h>
#include <random>

std::shared_ptr<metadata_manager> metadata_manager::instance = nullptr;
/******************************************************************************
 *Interface
 ******************************************************************************/
bool metadata_manager::is_created(std::string filename) {
  auto iter = file_map.find(filename);
  return iter != file_map.end();
}

int metadata_manager::create(std::string filename, std::string mode,
                             FILE *&fh) {
  if (filename.length() > FILENAME_MAX)
    return MDM__FILENAME_MAX_LENGTH;
  auto map = labios_system::getInstance(service_i)->map_client();
  fh = fmemopen(nullptr, 1, mode.c_str());
  file_stat stat = {fh, 0, 0, mode, true};
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    file_map.erase(iter);
  fh_map.emplace(fh, filename);
  file_map.emplace(filename, stat);
  std::string fs_str = serialization_manager().serialize_file_stat(stat);
  map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
  /**
   * TODO: put in map for outstanding operations-> on fclose create a file
   * in the destination and flush buffer contents
   **/
  return SUCCESS;
}

bool metadata_manager::is_opened(std::string filename) {
  auto iter = file_map.find(filename);
  return iter != file_map.end() && iter->second.is_open;
}

int metadata_manager::update_on_open(std::string filename, std::string mode,
                                     FILE *&fh) {
#ifdef TIMERMDM
  Timer t = Timer();
  t.resumeTime();
#endif
  if (filename.length() > FILENAME_MAX)
    return MDM__FILENAME_MAX_LENGTH;
  auto map = labios_system::getInstance(service_i)->map_client();
  auto iter = file_map.find(filename);
  file_stat stat;
  if (iter != file_map.end()) {
    stat = iter->second;
    fh = stat.fh;
  } else {
    fh = fmemopen(nullptr, 1, mode.c_str());
    stat.file_size = 0;
    stat.fh = fh;
    stat.is_open = true;
    if (mode == "r" || mode == "r+") {
      stat.file_pointer = 0;
    } else if (mode == "w" || mode == "w+") {
      stat.file_pointer = 0;
      stat.file_size = 0;
      remove_chunks(filename);
    } else if (mode == "a" || mode == "a+") {
      stat.file_pointer = stat.file_size;
    }
  }
  iter = file_map.find(filename);
  if (iter != file_map.end())
    file_map.erase(iter);
  fh_map.emplace(fh, filename);
  file_map.emplace(filename, stat);
  std::string fs_str = serialization_manager().serialize_file_stat(stat);
  map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
#ifdef TIMERMDM
  std::cout << "metadata_manager::update_on_open()," << t.pauseTime() << "\n";
#endif
  return SUCCESS;
}

bool metadata_manager::is_opened(FILE *fh) {
  auto iter1 = fh_map.find(fh);
  if (iter1 != fh_map.end()) {
    auto iter = file_map.find(iter1->second);
    return iter != file_map.end() && iter->second.is_open;
  }
  return false;
}

int metadata_manager::remove_chunks(std::string &filename) {
  auto map = labios_system::getInstance(service_i)->map_client();
  std::string chunks_str =
      map->remove(table::FILE_CHUNK_DB, filename, std::to_string(-1));
  std::vector<std::string> chunks = string_split(chunks_str);
  for (const auto &chunk : chunks) {
    map->remove(table::CHUNK_DB, chunk, std::to_string(-1));
  }
  return SUCCESS;
}

int metadata_manager::update_on_close(FILE *&fh) {
  auto iter = fh_map.find(fh);
  if (iter != fh_map.end()) {
    auto iter2 = file_map.find(iter->second);
    if (iter2 != file_map.end()) {
      file_map.erase(iter2);
    }
    fh_map.erase(iter);
    std::fclose(fh);
  }
  return SUCCESS;
}

std::string metadata_manager::get_filename(FILE *fh) {
  auto iter = fh_map.find(fh);
  if (iter != fh_map.end())
    return iter->second;
  return nullptr;
}

std::size_t metadata_manager::get_filesize(std::string filename) {
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    return iter->second.file_size;
  return 0;
}

std::string metadata_manager::get_mode(std::string filename) {
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    return iter->second.mode;
  return nullptr;
}

long long int metadata_manager::get_fp(const std::string &filename) {
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    return static_cast<long long int>(iter->second.file_pointer);
  else
    return -1;
}

int metadata_manager::update_read_task_info(std::vector<read_task> task_ks,
                                            std::string filename) {
#ifdef TIMERMDM
  Timer t = Timer();
  t.resumeTime();
#endif
  auto map = labios_system::getInstance(service_i)->map_client();
  file_stat fs;
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    fs = iter->second;
  for (int i = 0; i < task_ks.size(); ++i) {
    auto task_k = task_ks[i];
    update_on_read(filename, task_k.source.size);
  }
  std::string fs_str = serialization_manager().serialize_file_stat(fs);
  map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
#ifdef TIMERMDM
  std::cout << "metadata_manager::update_read_task_info()," << t.pauseTime()
            << "\n";
#endif
  return 0;
}

int metadata_manager::update_write_task_info(std::vector<write_task> task_ks,
                                             std::string filename) {
  auto map = labios_system::getInstance(service_i)->map_client();
  file_stat fs;
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    fs = iter->second;
  for (int i = 0; i < task_ks.size(); ++i) {
    auto task_k = task_ks[i];
    update_on_write(task_k.source.filename, task_k.source.size,
                    task_k.source.offset);
    if (!task_k.meta_updated) {
      auto base_offset =
          (task_k.destination.offset / MAX_IO_UNIT) * MAX_IO_UNIT;
      chunk_meta cm;
      cm.actual_user_chunk = task_k.source;
      cm.destination = task_k.destination;
      std::string chunk_str = serialization_manager().serialize_chunk(cm);
      map->put(table::CHUNK_DB, filename + std::to_string(base_offset),
               chunk_str, std::to_string(-1));
    }
  }
  std::string fs_str = serialization_manager().serialize_file_stat(fs);
  map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
  return 0;
}

int metadata_manager::update_on_seek(std::string filename, size_t offset,
                                     size_t origin) {
  auto map = labios_system::getInstance(service_i)->map_client();
  serialization_manager sm = serialization_manager();
  auto iter = file_map.find(filename);
  if (iter != file_map.end()) {
    switch (origin) {
    case SEEK_SET: {
      if (offset <= iter->second.file_size && offset >= 0) {
        iter->second.file_pointer = offset;
      }
      break;
    }
    case SEEK_CUR: {
      if (iter->second.file_pointer + offset <= iter->second.file_size) {
        iter->second.file_pointer += offset;
      }
      break;
    }
    case SEEK_END: {
      if (offset <= iter->second.file_size) {
        iter->second.file_pointer = iter->second.file_size - offset;
      }
      break;
    }
    default:
      std::cerr << "fseek origin error\n";
      return MDM__UPDATE_ON_FSEEK_FAILED;
    }
    std::string fs_str = sm.serialize_file_stat(iter->second);
    map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
  }
  return SUCCESS;
}

void metadata_manager::update_on_read(std::string filename, size_t size) {
  std::shared_ptr<distributed_hashmap> map =
      labios_system::getInstance(service_i)->map_client();
  serialization_manager sm = serialization_manager();
  auto iter = file_map.find(filename);
  if (iter != file_map.end()) {
    file_stat fs = iter->second;
    fs.file_pointer += size;
    file_map[filename] = fs;
    std::string fs_str = sm.serialize_file_stat(fs);
    map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
  }
}

void metadata_manager::update_on_write(std::string filename, size_t size,
                                       size_t offset) {
  auto map = labios_system::getInstance(service_i)->map_client();
  serialization_manager sm = serialization_manager();
  auto iter = file_map.find(filename);
  if (iter != file_map.end()) {
    file_stat fs = iter->second;
    if (fs.file_size < offset + size) {
      fs.file_size = offset + size;
    }
    fs.file_pointer = offset + size;
    file_map[filename] = fs;
    std::string fs_str = sm.serialize_file_stat(fs);
    map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
  }
}

std::vector<chunk_meta> metadata_manager::fetch_chunks(read_task task) {
#ifdef TIMERMDM
  Timer t = Timer();
  t.resumeTime();
#endif
  auto map = labios_system::getInstance(service_i)->map_client();

  auto remaining_data = task.source.size;
  auto chunks = std::vector<chunk_meta>();
  chunk_meta cm;
  size_t source_offset = task.source.offset;
  while (remaining_data > 0) {
    auto base_offset = (source_offset / MAX_IO_UNIT) * MAX_IO_UNIT;
    auto chunk_str = map->get(
        table::CHUNK_DB, task.source.filename + std::to_string(base_offset),
        std::to_string(-1));
    size_t size_to_read;
    if (!chunk_str.empty()) {
      cm = serialization_manager().deserialize_chunk(chunk_str);
      size_t bucket_offset = source_offset - base_offset;
      cm.destination.offset = bucket_offset;

      if (bucket_offset + remaining_data <= MAX_IO_UNIT) {
        size_to_read = remaining_data;
      } else {
        size_to_read = MAX_IO_UNIT - bucket_offset;
      }
      cm.destination.size = size_to_read;
    } else {
      cm.actual_user_chunk.offset = source_offset;
      cm.actual_user_chunk.size = remaining_data;
      cm.actual_user_chunk.filename = task.source.filename;
      cm.actual_user_chunk.location = location_type::PFS;
      cm.destination.location = location_type::PFS;
      cm.destination.size = cm.actual_user_chunk.size;
      cm.destination.filename = cm.actual_user_chunk.filename;
      cm.destination.offset = cm.actual_user_chunk.offset;
      cm.destination.worker = -1;
      size_to_read = cm.actual_user_chunk.size;
    }
    chunks.push_back(cm);
    if (remaining_data >= size_to_read)
      remaining_data -= size_to_read;
    else
      remaining_data = 0;
    source_offset += size_to_read;
  }
#ifdef TIMERMDM
  std::cout << "metadata_manager::fetch_chunks()," << t.pauseTime() << "\n";
#endif
  return chunks;
}

int metadata_manager::update_write_task_info(write_task task_k,
                                             std::string filename,
                                             std::size_t io_size) {
#ifdef TIMERMDM
  Timer t = Timer();
  t.resumeTime();
#endif
  auto map = labios_system::getInstance(service_i)->map_client();
  file_stat fs;
  auto iter = file_map.find(filename);
  if (iter != file_map.end())
    fs = iter->second;
  update_on_write(task_k.source.filename, io_size, task_k.source.offset);
  if (!task_k.meta_updated) {
    auto chunk_index = (task_k.source.offset / MAX_IO_UNIT);
    auto base_offset = (task_k.source.offset / MAX_IO_UNIT) * MAX_IO_UNIT;
    chunk_meta cm;
    cm.actual_user_chunk = task_k.source;
    cm.destination = task_k.destination;
    std::string chunk_str = serialization_manager().serialize_chunk(cm);
    map->put(table::CHUNK_DB, filename + std::to_string(base_offset), chunk_str,
             std::to_string(-1));
  }
  std::string fs_str = serialization_manager().serialize_file_stat(fs);
  map->put(table::FILE_DB, filename, fs_str, std::to_string(-1));
#ifdef TIMERMDM
  std::cout << "metadata_manager::update_write_task_info()," << t.pauseTime()
            << "\n";
#endif
  return 0;
}
