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

#ifndef AETRIO_MAIN_SERIALIZATION_MANAGER_H
#define AETRIO_MAIN_SERIALIZATION_MANAGER_H

#include "../data_structures.h"
#include <cereal/archives/json.hpp>
#include <sstream>
#include <cereal/cereal.hpp>

class serialization_manager {
public:
    //TODO: explore binary with NATS and memcached
    std::string serialize_file_stat(file_stat stat);

    chunk_meta deserialize_chunk(std::string chunk_str);

    std::string serialize_chunk(chunk_meta meta);

    std::string serialize_task(task *task);

    task* deserialize_task(std::string string);
};


#endif //AETRIO_MAIN_SERIALIZATION_MANAGER_H
