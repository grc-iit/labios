//
// Created by hariharan on 2/23/18.
//

#ifndef PORUS_MAIN_UTILITY_H
#define PORUS_MAIN_UTILITY_H
#include <string>
#include <vector>
#include <cstring>

std::vector<std::string> string_split(std::string value,std::string delimiter=","){
    char *token = strtok(const_cast<char *>(value.c_str()), delimiter.c_str());
    std::vector<std::string> splits=std::vector<std::string>();
    while (token != NULL)
    {
        splits.push_back(token);
        token = strtok(NULL, delimiter.c_str());
    }
    return splits;
}
#endif //PORUS_MAIN_UTILITY_H
