/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include <random>
#include <iomanip>
#include <zconf.h>
#include <fcntl.h>
#include "posix.h"
#include "../common/return_codes.h"
#include "../../testing/trace_replayer.h"
#include "../common/timer.h"
#include "../common/utilities.h"

enum test_case{
    SIMPLE_WRITE=0,
    SIMPLE_READ=1,
    MULTI_WRITE=2,
    MULTI_READ=3,
    SIMPLE_MIXED=4,
    MULTI_MIXED=5,
    CM1_BASE=6,
    MONTAGE_BASE=7,
    HACC_BASE=8,
    KMEANS_BASE=9,
    CM1_TABIOS=10,
    MONTAGE_TABIOS=11,
    HACC_TABIOS=12,
    KMEANS_TABIOS=13,
};
/*
 * set test case
 */
test_case testCase=SIMPLE_READ;
/******************************************************************************
*Interface
******************************************************************************/
int simple_write();
int simple_read();
int multi_write();
int multi_read();

void gen_random(char *s, std::size_t len) {
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

void cm1_base(int argc, char** argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"test.dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();
    MPI_File outFile;
    global_timer.resumeTime();
    MPI_Info info;
    MPI_Info_create(&info);
    MPI_Info_set(info, "direct_write", "true");

    MPI_File_open(MPI_COMM_WORLD, filename.c_str(), MPI_MODE_CREATE |
                                                    MPI_MODE_RDWR,
                  info, &outFile);
    MPI_File_set_view(outFile, static_cast<MPI_Offset>(rank * io_per_teration),
                      MPI_CHAR,
                      MPI_CHAR,
                      "native",
                      MPI_INFO_NULL);
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                MPI_File_write(outFile,
                                       write_buf,
                                      static_cast<int>(write[0]),
                                       MPI_CHAR,
                                       MPI_STATUS_IGNORE);
                MPI_Barrier(MPI_COMM_WORLD);
                global_timer.pauseTime();
                current_offset+=write[0];
            }
        }
    }
    global_timer.resumeTime();
    MPI_File_close(&outFile);
    MPI_Barrier(MPI_COMM_WORLD);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_cm1_base,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "cm1_base(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}

void cm1_tabios(int argc, char** argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=aetrio::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                aetrio::fwrite(write_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
            }
        }
    }
    sleep(10*MAX_SCHEDULE_TIMER);
    global_timer.resumeTime();
    aetrio::fclose(fh);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_cm1_tabios,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
        std::cout << "Please enter an integer value: ";
        int i;
        std::cin >> i;
#ifdef TIMER
        std::cout << "cm1_tabios(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }
}

void hacc_base(int argc, char** argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string buf_path=argv[4];
    parse_opts(argc,argv);
    std::string filename=buf_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});

    size_t current_offset=0;
#ifdef TIMERBASE
        Timer wbb=Timer();
        wbb.resumeTime();
#endif
    Timer global_timer=Timer();
    global_timer.resumeTime();
//    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
//    int fd=open(filename.c_str(),O_CREAT|O_SYNC|O_RSYNC|O_RDWR|O_TRUNC, mode);
    FILE* fh = std::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
#ifdef TIMERBASE
    wbb.pauseTime();
#endif
    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char write_buf[item[0]];
                gen_random(write_buf,item[0]);
                global_timer.resumeTime();
#ifdef TIMERBASE
                wbb.resumeTime();
#endif
//                write(fd,write_buf,item[0]);
//                fsync(fd);
                std::fwrite(write_buf,sizeof(char),item[0],fh);
                MPI_Barrier(MPI_COMM_WORLD);
                global_timer.pauseTime();
#ifdef TIMERBASE
                wbb.pauseTime();
#endif
                current_offset+=item[0];
            }
        }
    }
#ifdef TIMERBASE
    wbb.resumeTime();
    if(rank == 0) std::cout << "hacc_base()::write_to_BB,"
              <<std::fixed<<std::setprecision(10)
              <<wbb.pauseTime()<<"\n";
    else wbb.pauseTime();
#endif
    auto read_buf=static_cast<char*>(calloc(io_per_teration, sizeof(char)));
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer rbb=Timer();
    rbb.resumeTime();
#endif
    //close(fd);
    std::fclose(fh);
//    int in=open(filename.c_str(),O_SYNC|O_RSYNC|O_RDONLY| mode);
//    read(in,read_buf,io_per_teration);
//    close(in);
    FILE* fh1 = std::fopen(filename.c_str(),"r");
    std::fread(read_buf,sizeof(char),io_per_teration,fh1);
    std::fclose(fh1);
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    if(rank == 0) std::cout << "hacc_base()::read_from_BB,"
              <<std::fixed<<std::setprecision(10)
              <<rbb.pauseTime()<<"\n";
    else rbb.pauseTime();
#endif
    std::string output=file_path+"final_"+std::to_string(rank)+".out";
#ifdef TIMERBASE
    Timer pfs=Timer();
    pfs.resumeTime();
#endif
//    int out=open(output.c_str(),O_CREAT|O_SYNC|O_WRONLY|O_TRUNC, mode);
//    write(out,read_buf,io_per_teration);
//    fsync(out);
//    close(out);
    FILE* fh2 = std::fopen(output.c_str(),"w");
    std::fwrite(read_buf,sizeof(char),io_per_teration,fh2);
    std::fflush(fh2);
    std::fclose(fh2);
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    if(rank == 0) std::cout << "hacc_base()::write_to_PFS,"
              <<std::fixed<<std::setprecision(10)
              <<pfs.pauseTime()<<"\n";
    else pfs.pauseTime();
#endif
    global_timer.pauseTime();

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_hacc_base,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "hacc_base(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}

void hacc_tabios(int argc, char **argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=aetrio::fopen(filename.c_str(),"+w");
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                aetrio::fwrite(write_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
            }
        }
    }
    aetrio::fclose(fh);
    auto out_buf=static_cast<char*>(malloc(io_per_teration/8));
    gen_random(out_buf,io_per_teration/8);
    MPI_Barrier(MPI_COMM_WORLD);

    global_timer.resumeTime();
//    fh=aetrio::fopen(filename.c_str(),"r+");
//    aetrio::fread(read_buf,sizeof(char),io_per_teration,fh);
//    aetrio::fclose(fh);

    std::string output=file_path+"final_"+std::to_string(rank)+".out";
    FILE* out=aetrio::fopen(output.c_str(),"+w");
    aetrio::fwrite(out_buf,sizeof(char),io_per_teration/8,out);
    aetrio::fclose(out);
    global_timer.pauseTime();

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_hacc_tabios,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "hacc_tabios(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}

void montage_base(int argc, char** argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    //FILE* fh=std::fopen(filename.c_str(),"w+");
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int fd=open(filename.c_str(),O_CREAT|O_SYNC|O_RSYNC|O_RDWR|O_TRUNC, mode);
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char write_buf[item[0]];
                gen_random(write_buf,item[0]);
                global_timer.resumeTime();
//                if(rank%2==0){
//                    write(fd,write_buf,item[0]);
//                    fsync(fd);
//                }
//                else{
//                    static_cast<int64_t>
//                    (std::chrono::duration_cast<std::chrono::microseconds>
//                            (std::chrono::system_clock::now().
//                                    time_since_epoch()).count());
//                }
                write(fd,write_buf,item[0]);
                fsync(fd);
                MPI_Barrier(MPI_COMM_WORLD);
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    //sleep(3*MAX_SCHEDULE_TIMER);
    global_timer.resumeTime();
    close(fd);
    global_timer.pauseTime();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char read_buf[item[0]];
                global_timer.resumeTime();
//                if(rank%2==0){
//                    static_cast<int64_t>
//                    (std::chrono::duration_cast<std::chrono::microseconds>
//                            (std::chrono::system_clock::now().
//                                    time_since_epoch()).count());
//                }
//                else{
//                    filename=file_path+"test_"+std::to_string(rank-1)+".dat";
//                    fd=open(filename.c_str(),O_SYNC|O_RSYNC|O_RDONLY| mode);
//                    read(fd,read_buf,item[0]);
//                    close(fd);
//                }
                fd=open(filename.c_str(),O_SYNC|O_RSYNC|O_RDONLY| mode);
                read(fd,read_buf,item[0]);
                close(fd);
                MPI_Barrier(MPI_COMM_WORLD);
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    global_timer.resumeTime();

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_montage_base,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "montage_base(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}

void montage_tabios(int argc, char **argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 16});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=aetrio::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                aetrio::fwrite(write_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
            }
        }
    }
    sleep(3*MAX_SCHEDULE_TIMER);
    char* buf=new char[io_per_teration];
    gen_random(buf,io_per_teration);
    aetrio::fseek(fh,0,SEEK_SET);
    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char read_buf[item[0]];
                gen_random(read_buf,item[0]);
                global_timer.resumeTime();
                aetrio::fwrite(read_buf,sizeof(char),item[0],fh);
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    global_timer.resumeTime();
    aetrio::fclose(fh);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_montage_tabios,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "montage_tabios(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}

void kmeans_base(int argc, char** argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({32*1024, 1024});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    //FILE* fh=std::fopen(filename.c_str(),"w+");
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int fd=open(filename.c_str(),O_CREAT|O_SYNC|O_RSYNC|O_RDWR|O_TRUNC, mode);
    global_timer.pauseTime();
    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    //std::fwrite(write_buf,sizeof(char),io_per_teration,fh);
    //std::fflush(fh);
    write(fd,write_buf,io_per_teration);
    fsync(fd);
    MPI_Barrier(MPI_COMM_WORLD);
    delete(write_buf);
    int count=0;
    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                if(count%16==0){
                    std::random_device rd;
                    std::mt19937 generator(rd());
                    std::uniform_int_distribution<int> dist(0, 15);
                    auto rand_offset =
                            static_cast<size_t>(dist(generator)*2*1024*1024);
                    global_timer.resumeTime();
                    //std::fseek(fh,rand_offset,SEEK_SET);
                    lseek(fd,rand_offset,SEEK_SET);
                    MPI_Barrier(MPI_COMM_WORLD);
                    global_timer.pauseTime();
                }
                char read_buf[item[0]];
                global_timer.resumeTime();
                //std::fread(read_buf,sizeof(char),read[0],fh);
                auto bytes = read(fd,read_buf,item[0]);
                MPI_Barrier(MPI_COMM_WORLD);
                //std::cout << "Read:" <<bytes<<"\n";
                global_timer.pauseTime();
                current_offset+=item[0];
                count++;
            }
        }
    }

    global_timer.resumeTime();
    //std::fclose(fh);
    close(fd);
    MPI_Barrier(MPI_COMM_WORLD);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_kmeans_base,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "kmeans_base(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}

void kmeans_tabios(int argc, char **argv) {
#ifdef TIMER
    Timer t=Timer();
    t.resumeTime();
#endif
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({32*1024, 1024});

    size_t current_offset=0;
    Timer global_timer=Timer();
    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    global_timer.resumeTime();
    FILE* fh=aetrio::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    aetrio::fwrite(write_buf,sizeof(char),io_per_teration,fh);

    size_t count=0;

    for(int i=0;i<iteration;++i){
        for(auto read:workload){
            for(int j=0;j<read[1];++j){
                if(count%16==0){
                    std::random_device rd;
                    std::mt19937 generator(rd());
                    std::uniform_int_distribution<int> dist(0, 15);
                    auto rand_offset =
                            static_cast<size_t>(dist(generator)*2*1024*1024);
                    global_timer.resumeTime();
                    aetrio::fseek(fh,rand_offset,SEEK_SET);
                    global_timer.pauseTime();
                    current_offset=rand_offset;
                }
                char read_buf[read[0]];
                global_timer.resumeTime();
                //aetrio::fread(read_buf,sizeof(char),read[0],fh);
                memcpy(read_buf,write_buf+current_offset,read[0]);
                global_timer.pauseTime();
                current_offset+=read[0];
                count++;
            }
        }
    }
    delete(write_buf);
    global_timer.resumeTime();
    aetrio::fclose(fh);
    global_timer.pauseTime();
    //MPI_Barrier(MPI_COMM_WORLD);
    //std::cerr << "Rank#"<<rank<<"\tCount:" <<count<<"\n";
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        std::cout << "average_kmeans_tabios,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
#ifdef TIMER
        std::cout << "kmeans_tabios(),"
                  <<std::fixed<<std::setprecision(10)
                  <<t.pauseTime()<<"\n";
#endif
    }

}



int simple_write(){
    FILE* fh=aetrio::fopen("test","w+");
    size_t size_of_io=32 * 1024 * 1024;
    auto t= static_cast<char *>(malloc(size_of_io));
    gen_random(t,size_of_io);
    std::cout << strlen(t)<<"\n";
    aetrio::fwrite(t,size_of_io,1,fh);
    aetrio::fclose(fh);
    std::string s(t,128);
    std::cout << "write data:\t"<< s <<std::endl;
    free(t);
    return 0;
}
int simple_read(){
    FILE* fh=aetrio::fopen("test","r+");
    if(fh== nullptr) std::cerr << "file could not be opened\n";

    size_t size_of_io=32 * 1024 * 1024;
    auto w= static_cast<char *>(malloc(size_of_io));
    gen_random(w,size_of_io);
    auto t= static_cast<char *>(malloc(size_of_io));
    aetrio::fread(t,size_of_io,1,fh);
    std::string s(t);
    std::string e(w);
    if (s==w) std::cout << "read data:\t"<< s.substr(0,128) <<std::endl;
    else std::cerr << "read data:\t"<< s.substr(0,128) <<std::endl;
    aetrio::fclose(fh);
    free(t);
    return 0;
}
int multi_write(){
    FILE* fh=aetrio::fopen("akougkas_test","w+");
    size_t size_of_io=64 * 1024;

    for(int i=0;i<4096;i++){
        auto t= static_cast<char *>(malloc(size_of_io));
        gen_random(t,size_of_io);
        //aetrio::fseek(fh,0,SEEK_SET);
        std::size_t count = aetrio::fwrite(t,size_of_io,1,fh);
        if(i%50==0){
            std::cout << i <<"\twrite data:\t"<< count <<std::endl;
        }
        free(t);
        //usleep(100000);
    }
    aetrio::fclose(fh);

    return 0;
}
int multi_read(){
    FILE* fh=aetrio::fopen("test","r+");
    size_t size_of_io=16 * 1024 * 1024;
    auto t= static_cast<char *>(malloc(size_of_io));
    for(int i=0;i<1024;i++){
        aetrio::fwrite(t,1,size_of_io,fh);
    }
    aetrio::fclose(fh);
    free(t);
    return 0;
}

int test_queue(int num_tasks)


/******************************************************************************
*Main
******************************************************************************/
int main(int argc, char** argv){
    aetrio::MPI_Init(&argc,&argv);
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string log_name=std::string(argv[0])+"_"+std::to_string(rank)+".log";
    //freopen(log_name.c_str(), "w+", stdout);
    if(rank==0){
        aetrio_system::getInstance(service::LIB)->map_client->purge();
        aetrio_system::getInstance(service::LIB)->map_server->purge();
        system("rm -rf /opt/temp/1/*");
        system("rm -rf /opt/temp/2/*");
        system("rm -rf /opt/temp/3/*");
        system("rm -rf /opt/temp/4/*");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    int return_val=0;
    if(argc > 1){
        testCase= static_cast<test_case>(atoi(argv[1]));
    }
    switch(testCase){
        case SIMPLE_WRITE:{
            return_val=simple_write();
            break;
        }
        case SIMPLE_READ:{
            return_val=simple_read();
            break;
        }
        case SIMPLE_MIXED:{
            return_val=simple_write();
            return_val=simple_read();
            break;
        }
        case MULTI_WRITE:{
            return_val=multi_write();
            break;
        }
        case MULTI_READ:{
            return_val=multi_read();
            break;
        }
        case MULTI_MIXED:{
            return_val=multi_write();
            return_val+=multi_read();
            break;
        }
        case CM1_BASE:{
            cm1_base(argc,argv);
            break;
        }
        case CM1_TABIOS:{
            cm1_tabios(argc,argv);
            break;
        }
        case MONTAGE_BASE:{
            montage_base(argc,argv);
            break;
        }
        case MONTAGE_TABIOS:{
            montage_tabios(argc,argv);
            break;
        }
        case HACC_BASE:{
            hacc_base(argc,argv);
            break;
        }
        case HACC_TABIOS:{
            hacc_tabios(argc,argv);
            break;
        }
        case KMEANS_BASE:{
            kmeans_base(argc,argv);
            break;
        }
        case KMEANS_TABIOS:{
            kmeans_tabios(argc,argv);
            break;
        }
    }

    aetrio::MPI_Finalize();

    return return_val;
}