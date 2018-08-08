/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include <random>
#include <iomanip>
#include <zconf.h>
#include <fcntl.h>
#include <malloc.h>
#include "posix.h"
#include "../common/return_codes.h"
#include "../../testing/trace_replayer.h"
#include "../common/timer.h"
#include "../common/utilities.h"
#include "../common/threadPool.h"
#include <sstream>
#include <fstream>

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
    KMEANS_TABIOS=13
};

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

float get_average_ts() {
    /* run system command du -s to calculate size of director. */
    std::string cmd="sh /home/cc/nfs/aetrio/scripts/calc_ts.sh";
    FILE *fp;
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
            result += buffer.data();
    }
    return std::stof(result);
}

float get_average_worker() {
    /* run system command du -s to calculate size of director. */
    std::string cmd="sh /home/cc/nfs/aetrio/scripts/calc_worker.sh";
    FILE *fp;
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
            result += buffer.data();
    }
    return std::stof(result);
}

void cm1_base(int argc, char** argv) {
    std::stringstream stream;
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
                MPI_File_write(outFile, write_buf,
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
        stream <<"cm1_base,"<<std::fixed<<std::setprecision(6)<<mean<<"\n";
        std::cerr << stream.str();
    }
}

void cm1_tabios(int argc, char** argv) {
#ifdef COLLECT
    system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    stream <<"cm1_tabios,"<<std::fixed<<std::setprecision(10);
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
                aetrio::fwrite_sync(write_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();

                current_offset+=write[0];
            }
        }
    }
    std::cerr <<"Write finished\n";
    //sleep(10*MAX_SCHEDULE_TIMER);
    global_timer.resumeTime();
    aetrio::fclose(fh);
    global_timer.pauseTime();

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    std::cerr <<"all reduce finished\n";
    if(rank == 0) {
//        std::cerr << "Please enter an integer value: ";
//        int i;
//        std::cin >> i;
#ifdef COLLECT
        double ts = get_average_ts();
        double worker = get_average_worker();
        stream << ts<<","<< worker << ",";
#endif
        stream <<mean<<"\n";
        std::cerr << stream.str();
    }
}

void hacc_base(int argc, char** argv) {
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    if(rank == 0) stream << "hacc_base()," <<std::fixed<<std::setprecision(10);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string buf_path=argv[4];
    parse_opts(argc,argv);
    std::string filename=buf_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();

    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer wbb=Timer();
    wbb.resumeTime();
#endif
//    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
//    int fd=open(filename.c_str(),O_CREAT|O_SYNC|O_RSYNC|O_RDWR|O_TRUNC, mode);
    FILE* fh = std::fopen(filename.c_str(),"w+");
#ifdef TIMERBASE
    wbb.pauseTime();
#endif
    global_timer.pauseTime();

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
    if(rank == 0) stream<<"write_to_BB," <<wbb.pauseTime()<<",";
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
    if(rank == 0) stream << "read_from_BB," <<rbb.pauseTime()<<",";
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
    if(rank == 0) stream << "write_to_PFS,"<<pfs.pauseTime()<<"\n";
#endif
    global_timer.pauseTime();

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        stream << "average,"<< mean << "\n";
        std::cerr << stream.str();
    }
}

void hacc_tabios(int argc, char** argv) {
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
#ifdef COLLECT
    if(rank==0) system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
    if(rank == 0) stream << "hacc_tabios()"<<std::fixed<<std::setprecision(10);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string buf_path=argv[4];
    parse_opts(argc,argv);
    std::string filename=buf_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();

    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer wbb=Timer();
    wbb.resumeTime();
#endif
    FILE* fh = aetrio::fopen(filename.c_str(),"w+");
#ifdef TIMERBASE
    wbb.pauseTime();
#endif
    global_timer.pauseTime();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char write_buf[item[0]];
                gen_random(write_buf,item[0]);
                global_timer.resumeTime();
#ifdef TIMERBASE
                wbb.resumeTime();
#endif
                aetrio::fwrite_sync(write_buf,sizeof(char),item[0],fh);
#ifdef TIMERBASE
                wbb.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
#ifdef TIMERBASE
    wbb.resumeTime();
    if(rank == 0) stream<<"write_to_BB,"<<wbb.pauseTime()<<",";
#endif

    char* read_buf=(char*)malloc(io_per_teration*sizeof(char));
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer rbb=Timer();
    rbb.resumeTime();
#endif
aetrio::fclose(fh);

#ifndef COLLECT
    FILE* fh1 = aetrio::fopen(filename.c_str(),"r+");
    auto bytes = aetrio::fread(read_buf,sizeof(char),io_per_teration,fh1);
    if(bytes != io_per_teration) std::cerr <<"Bytes read: " << bytes << "\n";
    aetrio::fclose(fh1);
#endif

#ifdef TIMERBASE
    rbb.pauseTime();
    auto read_time=rbb.elapsed_time;
    double read_sum;
    MPI_Allreduce(&read_time, &read_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double read_mean = read_sum / comm_size;
    if(rank == 0) stream << "read_from_BB," <<read_mean<<",";
#endif

    std::string output=file_path+"final_"+std::to_string(rank)+".out";
#ifdef TIMERBASE
    Timer pfs=Timer();
    pfs.resumeTime();
#endif
    FILE* fh2 = aetrio::fopen(output.c_str(),"w+");
    aetrio::fwrite_sync(read_buf,sizeof(char),io_per_teration,fh2);
    aetrio::fclose(fh2);
#ifdef TIMERBASE
    if(rank == 0) stream << "write_to_PFS," <<pfs.pauseTime()<<",";
#endif
    global_timer.pauseTime();

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
#ifdef COLLECT
        double ts = get_average_ts();
        double worker = get_average_worker();
        stream << ts<<","<< worker << ",";
#endif
        stream << "average,"<< mean << "\n";
        std::cerr << stream.str();
    }
}

void montage_base(int argc, char** argv) {
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string final_path=argv[4];
    parse_opts(argc,argv);
    std::string filename1=file_path+"file1_"+std::to_string(rank)+".dat";
    std::string filename2=file_path+"file2_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
#ifdef TIMERBASE
    Timer c=Timer();
    c.resumeTime();
#endif
    int count=0;
    for(auto i=0;i<32;++i){
        for(int j=0;j<comm_size*1024*128;++j){
            count+=1;
            auto result = count * j;
            result -=j;
        }
        count =0;
    }
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
#ifdef TIMERBASE
    if(rank == 0){
        stream << "montage_base(),"
               <<std::fixed<<std::setprecision(10)
               <<c.pauseTime()<<",";
    }
#endif
    Timer global_timer=Timer();
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer w=Timer();
    w.resumeTime();
#endif
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int fd1,fd2;
    if(rank%2==0){
        fd1=open(filename1.c_str(),O_CREAT|O_SYNC|O_DSYNC|O_WRONLY|O_TRUNC, mode);
        fd2=open(filename2.c_str(),O_CREAT|O_SYNC|O_DSYNC|O_WRONLY|O_TRUNC, mode);
    }
#ifdef TIMERBASE
    w.pauseTime();
#endif
    global_timer.pauseTime();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char write_buf[item[0]];
                gen_random(write_buf,item[0]);
                global_timer.resumeTime();
#ifdef TIMERBASE
        w.resumeTime();
#endif
                if(rank%2==0){
                    if(j%2==0){
                        write(fd1,write_buf,item[0]);
                        fsync(fd1);
                    }else{
                        write(fd2,write_buf,item[0]);
                        fsync(fd2);
                    }

                }
                MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
                w.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    w.resumeTime();
#endif
    if(rank%2==0){
        close(fd1);
        close(fd2);
    }
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    w.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << w.elapsed_time<<",";
#endif

#ifdef TIMERBASE
    Timer r=Timer();
    r.resumeTime();
#endif
    size_t align = 4096;
    global_timer.resumeTime();
    if(rank%2!=0){
        filename1=file_path+"file1_"+std::to_string(rank-1)+".dat";
        filename2=file_path+"file2_"+std::to_string(rank-1)+".dat";
        fd1=open(filename1.c_str(),O_DIRECT|O_RDONLY| mode);
        if(fd1==-1) std::cerr << "open() failed!\n";
        fd2=open(filename2.c_str(),O_DIRECT|O_RDONLY| mode);
        if(fd2==-1) std::cerr << "open() failed!\n";
    }
#ifdef TIMERBASE
    r.pauseTime();
#endif
    global_timer.pauseTime();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                void* read_buf;
                read_buf = memalign(align * 2, item[0] + align);
                if (read_buf == NULL) std::cerr <<"memalign\n";
                read_buf += align;
                global_timer.resumeTime();
#ifdef TIMERBASE
    r.resumeTime();
#endif
                if(rank%2!=0){
                    ssize_t bytes = 0;
                    bytes = read(fd1,read_buf,item[0]/2);
                    bytes+= read(fd2,read_buf,item[0]/2);
                    if(bytes!=item[0])
                        std::cerr << "Read() failed!"<<"Bytes:"<<bytes
                                  <<"\tError code:"<<errno << "\n";
                }
                MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
                r.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    r.resumeTime();
#endif
    if(rank%2!=0) {
        close(fd1);
        close(fd2);
    }
#ifdef TIMERBASE
    r.pauseTime();
#endif

#ifdef TIMERBASE
    if(rank == 0) stream <<r.elapsed_time<<",";
#endif

#ifdef TIMERBASE
    Timer a=Timer();
    a.resumeTime();
#endif
    std::string finalname=final_path+"final_"+std::to_string(rank)+".dat";
    std::fstream outfile;
    outfile.open(finalname, std::ios::out);
    global_timer.pauseTime();
    for(auto i=0;i<32;++i){
        for(int j=0;j<comm_size*1024*128;++j){
            count+=1;
            auto result = count * j;
            result -=j;
        }
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<int> dist(0, io_per_teration);
        auto rand =dist(generator);
        int x = (i+1)*rand;

    }
    char buff[1024*1024];
    gen_random(buff,1024*1024);
    global_timer.resumeTime();
    outfile << buff << std::endl;
    outfile.close();
    global_timer.pauseTime();

#ifdef TIMERBASE
    if(rank == 0) stream << a.pauseTime()<<",";
#endif

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        stream << "average," << mean << "\n";
        std::cerr << stream.str();
    }
}

void montage_tabios(int argc, char** argv) {
#ifdef COLLECT
    system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
#ifdef DEBUG
    if(rank == 0) std::cerr << "Running Montage in TABIOS\n";
#endif
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string final_path=argv[4];
    parse_opts(argc,argv);
    std::string filename1=file_path+"file1_"+std::to_string(rank)+".dat";
    std::string filename2=file_path+"file2_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    Timer c=Timer();
    c.resumeTime();
#endif
#ifdef DEBUG
    if(rank ==0) std::cerr << "Starting Simulation Phase\n";
#endif
    int count=0;
    for(auto i=0;i<32;++i){
        for(int j=0;j<comm_size*1024*128;++j){
            count+=1;
            auto result = count * j;
            result -=j;
        }
        count =0;
    }
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
#ifdef TIMERBASE
    c.pauseTime();
#endif
#ifdef TIMERBASE

    if(rank == 0){
        stream << "montage_tabios(),"
               <<std::fixed<<std::setprecision(10)
               <<c.elapsed_time<<",";
    }
#endif
#ifdef TIMERBASE
    Timer w=Timer();
#endif
#ifdef DEBUG
    if(rank == 0) std::cerr << "Starting Write Phase\n";
#endif
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fd1,*fd2;
    if(rank%2==0|| comm_size==1){
#ifdef TIMERBASE
        w.resumeTime();
#endif
        fd1=aetrio::fopen(filename1.c_str(),"w+");
        fd2=aetrio::fopen(filename2.c_str(),"w+");
#ifdef TIMERBASE
        w.pauseTime();
#endif
    }
    global_timer.pauseTime();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char write_buf[item[0]];
                gen_random(write_buf,item[0]);
                global_timer.resumeTime();
#ifdef TIMERBASE
                w.resumeTime();
#endif
                if(rank%2==0 || comm_size==1){
                    if(j%2==0){
                        aetrio::fwrite_sync(write_buf,sizeof(char),item[0],fd1);
                    }else{
                        aetrio::fwrite_sync(write_buf,sizeof(char),item[0],fd2);
                    }
                }
#ifdef TIMERBASE
                w.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }

    global_timer.resumeTime();
#ifdef TIMERBASE
    w.resumeTime();
#endif
    if(rank%2==0|| comm_size==1){
        aetrio::fclose(fd1);
        aetrio::fclose(fd2);
    }
#ifdef TIMERBASE
    w.pauseTime();
#endif
    global_timer.pauseTime();
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    if(rank == 0) stream << w.elapsed_time<<",";
#endif
#ifdef TIMERBASE
    Timer r=Timer();
#endif
#ifdef DEBUG
    if(rank == 0) std::cerr << "Starting Reading Phase\n";
#endif
    size_t align = 4096;
    global_timer.resumeTime();
#ifdef TIMERBASE
    r.resumeTime();
#endif
    if(rank%2!=0|| comm_size==1){
        if(comm_size==1){
            filename1=file_path+"file1_"+std::to_string(rank)+".dat";
            filename2=file_path+"file2_"+std::to_string(rank)+".dat";
        }else{
            filename1=file_path+"file1_"+std::to_string(rank-1)+".dat";
            filename2=file_path+"file2_"+std::to_string(rank-1)+".dat";
        }

        fd1=aetrio::fopen(filename1.c_str(),"r+");
        fd2=aetrio::fopen(filename2.c_str(),"r+");
    }
#ifdef TIMERBASE
    r.pauseTime();
#endif
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                char read_buf[item[0]];
                global_timer.resumeTime();

#ifdef TIMERBASE
                r.resumeTime();
#endif
                if(rank%2!=0 || comm_size==1){
                    ssize_t bytes = 0;
#ifndef COLLECT
                    bytes = aetrio::fread(read_buf,sizeof(char),item[0]/2,fd1);
                    bytes += aetrio::fread(read_buf,sizeof(char),item[0]/2,fd2);
                    if(bytes!=item[0])
                        std::cerr << "Read() failed!"<<"Bytes:"<<bytes
                                  <<"\tError code:"<<errno << "\n";
#endif
                }
#ifdef TIMERBASE
                r.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    r.resumeTime();
#endif
    if(rank%2!=0|| comm_size==1) {
        aetrio::fclose(fd1);
        aetrio::fclose(fd2);
    }
#ifdef TIMERBASE
    r.pauseTime();
#endif
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    if(rank == 1 || comm_size==1){
        double read_time=r.elapsed_time;
        if(comm_size>1){
            std::cerr << "Sending...\n";
            MPI_Ssend(&read_time,
                      1,
                      MPI_DOUBLE,
                      0,
                      0,
                      MPI_COMM_WORLD);
            std::cerr << "Sent!\n";
        }

        else stream <<read_time<<",";
    }
    if(rank==0 && comm_size>1){
        double read_time;
        MPI_Recv(&read_time,
                 1,
                 MPI_DOUBLE,
                 1,
                 0,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);
        stream <<read_time<<",";
    }
#endif
#ifdef DEBUG
    if(rank == 0) std::cerr << "Starting Analysis Phase\n";
#endif
#ifdef TIMERBASE
    Timer a=Timer();
    a.resumeTime();
#endif
    std::string finalname=final_path+"final_"+std::to_string(rank)+".dat";
    FILE* outfile;
    outfile=aetrio::fopen(finalname.c_str(), "w+");
    global_timer.pauseTime();
    for(auto i=0;i<32;++i){
        for(int j=0;j<comm_size*1024*128;++j){
            count+=1;
            auto result = count * j;
            result -=j;
        }
        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<int> dist(0, io_per_teration);
        auto rand =dist(generator);
        int x = (i+1)*rand;
    }
    char write_buf[1024*1024];
    gen_random(write_buf,1024*1024);
    global_timer.resumeTime();
    aetrio::fwrite_sync(write_buf,sizeof(char),1024*1024,outfile);
    aetrio::fclose(outfile);
#ifdef TIMERBASE
    a.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << a.elapsed_time<<",";
#endif

    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
#ifdef COLLECT
        double ts = get_average_ts();
        double worker = get_average_worker();
        stream << ts<<","<< worker << ",";
#endif
        stream << "average," << mean << "\n";
        std::cerr << stream.str();
        std::cerr << "Please enter an integer value: ";
        int i;
        std::cin >> i;
    }
}

void kmeans_base(int argc, char** argv) {
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    stream << "average_kmeans_base,"<<std::fixed << std::setprecision(10);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string pfs_path=argv[4];
    parse_opts(argc,argv);
    std::string filename=file_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({32*1024, 1024});
    size_t current_offset=0;
    size_t align = 4096;
    Timer global_timer=Timer();

    global_timer.resumeTime();
    FILE* fh=std::fopen(filename.c_str(),"w+");
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
#ifdef TIMERBASE
    Timer map=Timer();
    map.resumeTime();
#endif
    int fd=open(filename.c_str(),O_DIRECT|O_RDWR|O_TRUNC, mode);
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();

    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    std::fwrite(write_buf,sizeof(char),io_per_teration,fh);
    std::fflush(fh);
//    write(fd,write_buf,io_per_teration);
//    fsync(fd);
    MPI_Barrier(MPI_COMM_WORLD);
    delete(write_buf);
    int count=0;

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                if(count%32==0){
                    std::random_device rd;
                    std::mt19937 generator(rd());
                    std::uniform_int_distribution<int> dist(0, 31);
                    auto rand_offset =(dist(generator)*1*1024*1024);
                    global_timer.resumeTime();
#ifdef TIMERBASE
                    map.resumeTime();
#endif
                    std::fseek(fh,rand_offset,SEEK_SET);
                    //lseek(fd,rand_offset,SEEK_SET);
                    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
                    map.pauseTime();
#endif
                    global_timer.pauseTime();
                }
                void* read_buf;
                read_buf = memalign(align * 2, item[0] + align);
                if (read_buf == NULL) std::cerr <<"memalign\n";
                read_buf += align;
                global_timer.resumeTime();
#ifdef TIMERBASE
                map.resumeTime();
#endif
                //auto bytes = std::fread(read_buf,sizeof(char),item[0],fh);

                auto bytes = read(fd,read_buf,item[0]);
                if(bytes!=item[0]) std::cerr << "Read failed:" <<bytes<<"\n";
                MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
                map.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
                count++;
            }
        }
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    map.resumeTime();
#endif
    //std::fclose(fh);
    close(fd);
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << map.elapsed_time<<",";
#endif
#ifdef TIMERBASE
    Timer reduce=Timer();
#endif
    MPI_File outFile;
    MPI_Info info;
    MPI_Info_create(&info);
    MPI_Info_set(info, "direct_write", "true");
    std::string final=pfs_path+"final.dat";
    global_timer.resumeTime();
#ifdef TIMERBASE
    reduce.resumeTime();
#endif
    MPI_File_open(MPI_COMM_WORLD, final.c_str(), MPI_MODE_CREATE |
                                                    MPI_MODE_RDWR,
                  info, &outFile);
    MPI_File_set_view(outFile, static_cast<MPI_Offset>(rank * 1024*1024),
                      MPI_CHAR,
                      MPI_CHAR,
                      "native",
                      MPI_INFO_NULL);
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();
    char out_buff[1024*1024];
    gen_random(out_buff,1024*1024);
    global_timer.resumeTime();
#ifdef TIMERBASE
    reduce.resumeTime();
#endif
    MPI_File_write(outFile,
                   out_buff,
                   (1024*1024),
                   MPI_CHAR,
                   MPI_STATUS_IGNORE);
    MPI_File_close(&outFile);
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << reduce.elapsed_time<<",";
#endif
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        stream << "average,"<< mean << "\n";
        std::cerr << stream.str();
    }
}

void wait_for_read(size_t size,std::vector<read_task> tasks,std::string
filename){
    char read_buf[size];
    auto bytes= aetrio::fread_wait(read_buf,tasks,filename);
    if(bytes!=size) std::cerr << "Read failed:" <<bytes<<"\n";
}


void kmeans_tabios(int argc, char **argv) {
#ifdef COLLECT
    system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    stream << "average_kmeans_tabios,"<<std::fixed << std::setprecision(10);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string pfs_path=argv[4];
    parse_opts(argc,argv);
    std::string filename=file_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({32*1024, 1024});

    size_t current_offset=0;
    Timer global_timer=Timer();
    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer map=Timer();
    map.resumeTime();
#endif
    FILE* fh=aetrio::fopen(filename.c_str(),"w+");
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
    aetrio::fwrite_sync(write_buf,sizeof(char),io_per_teration,fh);
    delete(write_buf);
    size_t count=0;
    std::vector<std::pair<size_t,std::vector<read_task>>> operations=
            std::vector<std::pair<size_t,std::vector<read_task>>>();
    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                if(count%32==0){
                    std::random_device rd;
                    std::mt19937 generator(rd());
                    std::uniform_int_distribution<int> dist(0, 31);
                    auto rand_offset = (dist(generator)*1*1024*1024);
                    global_timer.resumeTime();
#ifdef TIMERBASE
                    map.resumeTime();
#endif
                    aetrio::fseek(fh,rand_offset,SEEK_SET);
#ifdef TIMERBASE
                    map.pauseTime();
#endif
                    global_timer.pauseTime();
                    current_offset= static_cast<size_t>(rand_offset);
                }
                global_timer.resumeTime();
#ifdef TIMERBASE
                map.resumeTime();
#endif
#ifndef COLLECT
                operations.emplace_back(std::make_pair(item[0],
                        aetrio::fread_async(sizeof(char), item[0],fh)));

#endif
#ifdef TIMERBASE
                map.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
                count++;
            }
        }
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    map.resumeTime();
#endif
    int wait_count=1;
    for(auto operation:operations){
          wait_for_read(operation.first, operation.second,filename);
    }
    aetrio::fclose(fh);
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << map.elapsed_time<<",";
#endif
    std::string finalname=pfs_path+"final_"+std::to_string(rank)+".dat";
    FILE* outfile;
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer reduce=Timer();
    reduce.resumeTime();
#endif
    outfile=aetrio::fopen(finalname.c_str(), "w+");
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();

    char out_buff[1024*1024];
    gen_random(out_buff,1024*1024);
    global_timer.resumeTime();
#ifdef TIMERBASE
    reduce.resumeTime();
#endif
    aetrio::fwrite_sync(out_buff,sizeof(char),1024*1024,outfile);
    aetrio::fclose(outfile);
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << reduce.elapsed_time<<",";
#endif
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
#ifdef COLLECT
        double ts = get_average_ts();
        double worker = get_average_worker();
        stream << ts<<","<< worker << ",";
#endif
        stream << "average,"<< mean<< "\n";
        std::cerr << stream.str();
    }

}

void kmeans_tabios_sync(int argc, char **argv) {
#ifdef COLLECT
    system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
    std::stringstream stream;
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    stream << "average_kmeans_tabios,"<<std::fixed << std::setprecision(10);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    std::string pfs_path=argv[4];
    parse_opts(argc,argv);
    std::string filename=file_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({32*1024, 1024});

    size_t current_offset=0;
    Timer global_timer=Timer();
    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer map=Timer();
    map.resumeTime();
#endif
    FILE* fh=aetrio::fopen(filename.c_str(),"w+");
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
    aetrio::fwrite_sync(write_buf,sizeof(char),io_per_teration,fh);
    delete(write_buf);
    size_t count=0;

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                if(count%32==0){
                    std::random_device rd;
                    std::mt19937 generator(rd());
                    std::uniform_int_distribution<int> dist(0, 31);
                    auto rand_offset = (dist(generator)*1*1024*1024);
                    global_timer.resumeTime();
#ifdef TIMERBASE
                    map.resumeTime();
#endif
                    aetrio::fseek(fh,rand_offset,SEEK_SET);
#ifdef TIMERBASE
                    map.pauseTime();
#endif
                    global_timer.pauseTime();
                    current_offset= static_cast<size_t>(rand_offset);
                }
                char read_buf[item[0]];
                global_timer.resumeTime();
#ifdef TIMERBASE
                map.resumeTime();
#endif
#ifndef COLLECT
                auto bytes = aetrio::fread(read_buf,sizeof(char),item[0],fh);
                if(bytes!=item[0]) std::cerr << "Read failed:" <<bytes<<"\n";
#endif
#ifdef TIMERBASE
                map.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
                count++;
            }
        }
    }

    global_timer.resumeTime();
#ifdef TIMERBASE
    map.resumeTime();
#endif
    aetrio::fclose(fh);
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << map.elapsed_time<<",";
#endif
    std::string finalname=pfs_path+"final_"+std::to_string(rank)+".dat";
    FILE* outfile;
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer reduce=Timer();
    reduce.resumeTime();
#endif
    outfile=aetrio::fopen(finalname.c_str(), "w+");
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();

    char out_buff[1024*1024];
    gen_random(out_buff,1024*1024);
    global_timer.resumeTime();
#ifdef TIMERBASE
    reduce.resumeTime();
#endif
    aetrio::fwrite_sync(out_buff,sizeof(char),1024*1024,outfile);
    aetrio::fclose(outfile);
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    if(rank == 0) stream << reduce.elapsed_time<<",";
#endif
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
#ifdef COLLECT
        double ts = get_average_ts();
        double worker = get_average_worker();
        stream << ts<<","<< worker << ",";
#endif
        stream << "average,"<< mean<< "\n";
        std::cerr << stream.str();
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

/******************************************************************************
*Main
******************************************************************************/
int main(int argc, char** argv){
    aetrio::MPI_Init(&argc,&argv);
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    if(rank==0){
        std::string log_name=std::string(argv[0])+"_" +
                             std::to_string(comm_size) +".csv";
        freopen(log_name.c_str(), "w+", stdout);
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



