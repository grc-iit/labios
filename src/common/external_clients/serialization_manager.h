//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_SERIALIZATION_MANAGER_H
#define PORUS_MAIN_SERIALIZATION_MANAGER_H


#include "../data_structures.h"
#include <string>
#include <cereal/archives/json.hpp>
#include <sstream>
#include <cereal/cereal.hpp>
class serialization_manager {
public:
    std::string serialise_file_stat(file_stat stat);

    chunk_meta deserialise_chunk(std::string chunk_str);

    std::string serialise_chunk(chunk_meta meta);

    std::string serialise_task(task* task);

    task deserialise_task(const char *string);
};


#endif //PORUS_MAIN_SERIALIZATION_MANAGER_H
