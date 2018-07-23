//
// Created by anthony on 7/22/18.
//

#include <iomanip>
#include "trace_replayer.h"
#include "../src/common/timer.h"

void trace_replayer::gen_random(char *s, std::size_t len) {
    static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
    std::default_random_engine generator;
    std::uniform_int_distribution<int> dist(1, 1000000);
    for (int i = 0; i < len; ++i) {
        s[i] = alphanum[dist(generator) % (sizeof(alphanum) - 1)];
    }

    s[len] = 0;
}

int trace_replayer::prepare_data(std::string path, std::string traceName,
                                 char * filename, int rank) {
    /*Initialization of some stuff*/
    std::string traceFile=path+traceName;
    FILE* trace;
    FILE* file = nullptr;
    char* line = nullptr;
    int comm_size;

    size_t len=0;
    ssize_t readsize;
    std::string operation;
    long offset = 0;
    long request_size = 0;
    char* word;
    line = (char*) malloc(128);
    std::vector<double> timings;
    double average=0;

    /* Do the I/O and comparison*/
    /*Opening the trace file*/
    trace = std::fopen(traceFile.c_str(), "r");
    if (trace==NULL) {
        return 0;
    }
    /*system("/home/anthony/Dropbox/ReSearch/Projects/iris/scripts/clearcache"
               ".sh");*/
    /*While loop to read each line from the trace and create I/O*/
    time_t now = time(0);
    char *dt = ctime(&now);
    std::cout << traceName << "," << dt;
    int lineNumber=0;
    while ((readsize = getline(&line, &len, trace)) != -1) {
        if (readsize < 4) {
            break;
        }
        word = strtok(line, ",");
        operation = word;
        word = strtok(NULL, ",");
        offset = atol(word);
        word = strtok(NULL, ",");
        request_size = atol(word);


        Timer operationTimer = Timer();
        operationTimer.startTime();
        if (operation == "FOPEN") {
            file = open(filename, "w+");
        } else if (operation == "FCLOSE") {
            close(file);
        } else if (operation == "READ") {
            char writebuf[request_size];
            gen_random(writebuf,request_size);
            std::cout <<writebuf<<"\n";
            seek(file, (size_t) offset);
            write(writebuf, file, (size_t) request_size);
        } else if (operation == "WRITE") {
        } else if (operation == "LSEEK") {
            seek(file, (size_t) offset);
        }
        lineNumber++;
    }
    std::fclose(trace);
    MPI_Barrier(MPI_COMM_WORLD);
    if(rank == 0) {
        printf("Prepare Finished\n");
    }
    if (line) free(line);
    return 0;
}

int trace_replayer::replay_trace(std::string path, std::string traceName,
                                 char *filename, int repetitions, int rank) {
    /*Initialization of some stuff*/
    std::string traceFile=path+traceName;
    FILE* trace;
    FILE* file = nullptr;
    char* line = nullptr;
    int comm_size;

    size_t len=0;
    ssize_t readsize;
    std::string operation;
    long offset = 0;
    long request_size = 0;
    char* word;
    line = (char*) malloc(128);
    std::vector<double> timings;
    double average=0;
    int rep =  repetitions;

    /* Do the I/O and comparison*/
    while(rep) {
        /*Opening the trace file*/
        trace = std::fopen(traceFile.c_str(), "r");
        if (trace==NULL) {
            return 0;
        }
        /*system("/home/anthony/Dropbox/ReSearch/Projects/iris/scripts/clearcache"
                   ".sh");*/
        /*While loop to read each line from the trace and create I/O*/
        Timer globalTimer = Timer();
        globalTimer.startTime();
        time_t now = time(0);
        char *dt = ctime(&now);
        std::cout << traceName << "," << dt;
        int lineNumber=0;
        while ((readsize = getline(&line, &len, trace)) != -1) {
            if (readsize < 4) {
                break;
            }
            word = strtok(line, ",");
            operation = word;
            word = strtok(NULL, ",");
            offset = atol(word);
            word = strtok(NULL, ",");
            request_size = atol(word);


            Timer operationTimer = Timer();
            operationTimer.startTime();
            if (operation == "FOPEN") {
                file = open(filename, "w+");
            } else if (operation == "FCLOSE") {
                close(file);
            } else if (operation == "WRITE") {
                char writebuf[request_size];
                gen_random(writebuf,request_size);
                //std::cout <<writebuf<<"\n";
                seek(file, (size_t) offset);
                write(writebuf, file, (size_t) request_size);
            } else if (operation == "READ") {
                char readbuf[request_size];
                seek(file, (size_t) offset);
                read(readbuf, file, (size_t) request_size);
                if(readbuf) delete(readbuf);
            } else if (operation == "LSEEK") {
                seek(file, (size_t) offset);
            }
            operationTimer.endTimeWithPrint(operation + "," + std::to_string(offset) + ","
                                               + std::to_string(request_size) + ",");

            lineNumber++;
        }
#ifdef TAPIOS
        std::cout << "TAPIOS,";
#else
        std::cout << "POSIX,";
#endif
        timings.emplace_back(globalTimer.stopTime());
        rep--;

        std::fclose(trace);
    }
    for(auto timing:timings){
        average +=timing;
    }
    average=average/repetitions;
    double global_time;
#ifdef MPI_ENABLE
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  MPI_Allreduce(&average, &global_time, 1, MPI_DOUBLE, MPI_SUM,
                MPI_COMM_WORLD);
#else
    global_time=average;
    comm_size=1;
#endif
    double mean = global_time / comm_size;

    if(rank == 0) {
        printf("Time : %lf\n",mean);
        std::cout <<
                  #ifdef TAPIOS
                  "TAPIOS,"
                  #else
                  "POSIX,"
                  #endif
                  << "average,"
                  << std::setprecision(6)
                  #ifdef IRIS
                  << average/repetitions
                  #else
                  << mean
                  #endif
                  << "\n";
    }
    if (line) free(line);

#ifdef TAPIOS

#else
    /*if( remove( "/mnt/orangefs/temp/file.dat" ) != 0 )
      perror( "Error deleting file" );*/
#endif

    return 0;
}


