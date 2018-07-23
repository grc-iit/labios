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
        return aetrio::fopen(name,mode);
    }

    static int close(FILE *fh) {
        return aetrio::fclose(fh);
    }

    static size_t read(void* ptr, FILE *fh, size_t amount) {
        return aetrio::fread(ptr, sizeof(char), amount, fh);
    }

    static size_t write(void* ptr, FILE *fh, size_t amount) {
        return aetrio::fwrite(ptr, sizeof(char), amount, fh);
    }


    static int seek(FILE *fh, size_t amount) {
        aetrio::fseek(fh, amount, SEEK_SET);
        return 0;
    }

    static int prepare_data(std::string path, std::string traceName);
    static int replay_trace(std::string path, std::string traceName,
                            char * filename, int repetitions, int rank);

private:
    static void gen_random(char *s, std::size_t len);
};


#endif //AETRIO_TRACE_REPLAYER_H
