//
// Created by anthony on 7/22/18.
//

#ifndef AETRIO_TRACE_REPLAYER_H
#define AETRIO_TRACE_REPLAYER_H


#include <cstdio>
#include <random>
#include "../src/lib/posix.h"

class trace_replayer{
public:
    static FILE * open(const char *name, const char *mode) {
#ifdef TAPIOS
        return aetrio::fopen(name,mode);
#else
        return std::fopen(name,mode);
#endif
    }

    static int close(FILE *fh) {
#ifdef TAPIOS
        return aetrio::fclose(fh);
#else
        return std::fclose(fh);
#endif
    }

    static size_t read(void* ptr, FILE *fh, size_t amount) {
#ifdef TAPIOS
        return aetrio::fread(ptr, sizeof(char), amount, fh);
#else
        return std::fread(ptr, sizeof(char), amount, fh);
#endif
    }

    static size_t write(void* ptr, FILE *fh, size_t amount) {
#ifdef TAPIOS
        return aetrio::fwrite(ptr, sizeof(char), amount, fh);
#else
        return std::fwrite(ptr, sizeof(char), amount, fh);
#endif
    }


    static int seek(FILE *fh, size_t amount) {
#ifdef TAPIOS
        return aetrio::fseek(fh, amount, SEEK_SET);
#else
        return std::fseek(fh, amount, SEEK_SET);
#endif
    }

    static int prepare_data(std::string path, std::string traceName,
                            char * filename, int rank);
    static int replay_trace(std::string path, std::string traceName,
                            char * filename, int repetitions, int rank);

private:
    static void gen_random(char *s, std::size_t len);
};


#endif //AETRIO_TRACE_REPLAYER_H
