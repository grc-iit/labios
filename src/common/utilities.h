//
// Created by hariharan on 2/23/18.
//

#ifndef AETRIO_MAIN_UTILITY_H
#define AETRIO_MAIN_UTILITY_H

#include <string>
#include <vector>
#include <cstring>
#include <getopt.h>
#include "config_manager.h"

static std::vector<std::string> string_split(std::string value,std::string delimiter=","){
    char *token = strtok(const_cast<char *>(value.c_str()), delimiter.c_str());
    std::vector<std::string> splits=std::vector<std::string>();
    while (token != NULL)
    {
        splits.push_back(token);
        token = strtok(NULL, delimiter.c_str());
    }
    return splits;
}
static int parse_opts(int argc, char *argv[]){
    char* argv_cp[argc];
    for(int i=0;i<argc;++i){
        argv_cp[i]=new char[strlen(argv[i])+1];
        strcpy(argv_cp[i],argv[i]);
    }

    auto conf=config_manager::get_instance();
    int flags, opt;
    int nsecs, tfnd;

    nsecs = 0;
    tfnd = 0;
    flags = 0;
    while ((opt = getopt (argc, argv_cp, "a:b:c:d:")) != -1)
    {
        switch (opt)
        {
            case 'a':{
                conf->NATS_URL_CLIENT=std::string(optarg);
                break;
            }
            case 'b':{
                conf->NATS_URL_SERVER=std::string(optarg);
                break;
            }
            case 'c':{
                conf->MEMCACHED_URL_CLIENT=std::string(optarg);
                break;
            }
            case 'd':{
                conf->MEMCACHED_URL_SERVER=std::string(optarg);
                break;
            }
            default:{
break;
            }

        }
    }
    for(int i=0;i<argc;++i){
       delete(argv_cp[i]);
    }
    return 0;
}

#endif //AETRIO_MAIN_UTILITY_H
