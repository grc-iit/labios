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
