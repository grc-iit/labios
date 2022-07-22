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
/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include <random>
#include <iomanip>
#include <zconf.h>
#include <fcntl.h>
#include <malloc.h>
#include "labios/drivers/posix.h"
#include "labios/common/return_codes.h"
#include "labios/common/timer.h"
#include "labios/common/utilities.h"
#include "labios/common/threadPool.h"
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
    KMEANS_TABIOS=13,
    STRESS_TEST=14
};

test_case testCase=SIMPLE_READ;
/******************************************************************************
*Interface
******************************************************************************/

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
    std::string filename=file_path+"test.dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();
    MPI_File outFile;
    char* write_buf[32];
    for(int i=0;i<32;++i){
        write_buf[i]= static_cast<char *>(malloc(1 * 1024 * 1024));
        gen_random(write_buf[i],1*1024*1024);
    }
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
                global_timer.resumeTime();
                MPI_File_write(outFile, write_buf[j],
                               static_cast<int>(write[0]),
                               MPI_CHAR,
                               MPI_STATUS_IGNORE);
                MPI_Barrier(MPI_COMM_WORLD);
                global_timer.pauseTime();
                current_offset+=write[0];
            }
        }
    }
    for(int i=0;i<32;++i){
        free(write_buf[i]);
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
void simulation(int comp){
    int count=0;
    int comm_size;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::cout << "\tSimulation Started";
    for(auto i=0;i<32;++i){
        for(int j=0;j<comp*1024*1024;++j){
            count+=1;
            auto result = count * j;
            result -=j;
        }
        std::cout << ".";
        count =0;
    }
    std::cout << "\tSimulation completed\n";
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
    FILE* fh=labios::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();


    char* write_buf[32];
    for(int i=0;i<32;++i){
        write_buf[i]= static_cast<char *>(malloc(1 * 1024 * 1024));
        gen_random(write_buf[i],1*1024*1024);
    }
    std::vector<std::pair<size_t,std::vector<write_task*>>> operations=
            std::vector<std::pair<size_t,std::vector<write_task*>>>();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                global_timer.resumeTime();
                operations.emplace_back
                (std::make_pair(item[0],
                        labios::fwrite_async(write_buf[j],sizeof(char),
                                item[0],fh)));
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    global_timer.resumeTime();
    for(auto operation:operations){
        auto bytes = labios::fwrite_wait(operation.second);
        if(bytes != operation.first) std::cerr << "Write failed\n";
    }
    global_timer.pauseTime();
    for(int i=0;i<32;++i){
        free(write_buf[i]);
    }
    global_timer.resumeTime();
    if(rank==0) std::cerr <<"Write finished\n";
    labios::fclose(fh);
    global_timer.pauseTime();

    auto time=global_timer.elapsed_time;
    double sum,max,min;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
#ifdef COLLECT
        double ts = get_average_ts();
        double worker = get_average_worker();
        stream << ts<<","<< worker << ",";
#endif
        stream <<mean<<","<<max<<","<<min<<"\n";
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
    std::string filename=buf_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();
    char* write_buf[32];
    for(int i=0;i<32;++i){
        write_buf[i]= static_cast<char *>(malloc(1 * 1024 * 1024));
        gen_random(write_buf[i],1*1024*1024);
    }
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
                global_timer.resumeTime();
#ifdef TIMERBASE
                wbb.resumeTime();
#endif
//                write(fd,write_buf,item[0]);
//                fsync(fd);
                std::fwrite(write_buf[j],sizeof(char),item[0],fh);
                MPI_Barrier(MPI_COMM_WORLD);
                global_timer.pauseTime();
#ifdef TIMERBASE
                wbb.pauseTime();
#endif
                current_offset+=item[0];
            }
        }
    }
    for(int i=0;i<32;++i){
        free(write_buf[i]);
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
    free(read_buf);
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
    std::string filename=buf_path+"test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();
    char* write_buf[32];
    for(int i=0;i<32;++i){
        write_buf[i]= static_cast<char *>(malloc(1 * 1024 * 1024));
        gen_random(write_buf[i],1*1024*1024);
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer wbb=Timer();
    wbb.resumeTime();
#endif
    FILE* fh = labios::fopen(filename.c_str(),"w+");
#ifdef TIMERBASE
    wbb.pauseTime();
#endif
    global_timer.pauseTime();

    std::vector<std::pair<size_t,std::vector<write_task*>>> operations=
            std::vector<std::pair<size_t,std::vector<write_task*>>>();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                global_timer.resumeTime();
#ifdef TIMERBASE
                wbb.resumeTime();
#endif
                operations.emplace_back(std::make_pair(item[0],
                        labios::fwrite_async(write_buf[j],sizeof(char),
                                item[0], fh)));
#ifdef TIMERBASE
                wbb.pauseTime();
#endif
                global_timer.pauseTime();
                current_offset+=item[0];
            }
        }
    }
    for(int i=0;i<32;++i){
        free(write_buf[i]);
    }
    global_timer.resumeTime();
#ifdef TIMERBASE
    wbb.resumeTime();
#endif
    for(auto operation:operations){
        auto bytes = labios::fwrite_wait(operation.second);
        if(bytes != operation.first) std::cerr << "Write failed\n";
    }
#ifdef TIMERBASE
    wbb.pauseTime();
#endif
#ifdef TIMERBASE
    auto writeBB=wbb.elapsed_time;
    double bb_sum;
    MPI_Allreduce(&writeBB, &bb_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double bb_mean = bb_sum / comm_size;
    if(rank == 0) stream<<"write_to_BB,"<<bb_mean<<",";
#endif

    char* read_buf=(char*)malloc(io_per_teration*sizeof(char));
    gen_random(read_buf,io_per_teration);
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer rbb=Timer();
    rbb.resumeTime();
#endif
    labios::fclose(fh);

#ifndef COLLECT
    FILE* fh1 = labios::fopen(filename.c_str(),"r+");
    auto op = labios::fread_async(sizeof(char),io_per_teration, fh1);
//    auto bytes= labios::fread_wait(read_buf,op,filename);
//    if(bytes!=io_per_teration) std::cerr << "Read failed:" <<bytes<<"\n";
    labios::fclose(fh1);

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
    FILE* fh2 = labios::fopen(output.c_str(),"w+");
    labios::fwrite_async(read_buf, sizeof(char), io_per_teration, fh2);
    labios::fclose(fh2);
#ifdef TIMERBASE
    pfs.pauseTime();
    free(read_buf);
    auto pfs_time=pfs.elapsed_time;
    double pfs_sum;
    MPI_Allreduce(&pfs_time, &pfs_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double pfs_mean = pfs_sum / comm_size;
    if(rank == 0) stream << "write_to_PFS," <<pfs_mean<<",";
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
    char* write_buf[32];
    for(int i=0;i<32;++i){
        write_buf[i]= static_cast<char *>(malloc(1 * 1024 * 1024));
        gen_random(write_buf[i],1*1024*1024);
    }
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
                global_timer.resumeTime();
#ifdef TIMERBASE
                w.resumeTime();
#endif
                if(rank%2==0){
                    if(j%2==0){
                        write(fd1,write_buf[j],item[0]);
                        fsync(fd1);
                    }else{
                        write(fd2,write_buf[j],item[0]);
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
    for(int i=0;i<32;++i){
        free(write_buf[i]);
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
    char* write_buf[32];
    for(int i=0;i<32;++i){
        write_buf[i]= static_cast<char *>(malloc(1 * 1024 * 1024));
        gen_random(write_buf[i],1*1024*1024);
    }
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
        fd1=labios::fopen(filename1.c_str(),"w+");
        fd2=labios::fopen(filename2.c_str(),"w+");
#ifdef TIMERBASE
        w.pauseTime();
#endif
    }
    global_timer.pauseTime();
    std::vector<std::pair<size_t,std::vector<write_task*>>> operations=
            std::vector<std::pair<size_t,std::vector<write_task*>>>();

    for(int i=0;i<iteration;++i){
        for(auto item:workload){
            for(int j=0;j<item[1];++j){
                global_timer.resumeTime();
#ifdef TIMERBASE
                w.resumeTime();
#endif
                if(rank%2==0 || comm_size==1){
                    if(j%2==0){
                        operations.emplace_back(
                                std::make_pair(item[0], labios::fwrite_async(write_buf[j],sizeof(char),item[0], fd1)));
                    }else{
                        operations.emplace_back(
                                std::make_pair(item[0],labios::fwrite_async
                                (write_buf[j],sizeof(char),item[0], fd2)));
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
        for(auto operation:operations){
            auto bytes = labios::fwrite_wait(operation.second);
            if(bytes != operation.first) std::cerr << "Write failed\n";
        }
        labios::fclose(fd1);
        labios::fclose(fd2);
    }
#ifdef TIMERBASE
    w.pauseTime();
#endif
    global_timer.pauseTime();
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE

    double w_time=0.0;
    if(rank%2==0)
        w_time=w.elapsed_time;
    double w_sum;
    MPI_Allreduce(&w_time, &w_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double w_mean = w_sum / comm_size*2;
    if(rank == 0) stream << w_mean<<",";
#endif
    for(int i=0;i<32;++i){
        free(write_buf[i]);
    }
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

        fd1=labios::fopen(filename1.c_str(),"r+");
        fd2=labios::fopen(filename2.c_str(),"r+");
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
                    bytes = labios::fread(read_buf,sizeof(char),item[0]/2,fd1);
                    bytes += labios::fread(read_buf,sizeof(char),item[0]/2,fd2);
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
        labios::fclose(fd1);
        labios::fclose(fd2);
    }
#ifdef TIMERBASE
    r.pauseTime();
#endif
    MPI_Barrier(MPI_COMM_WORLD);
    global_timer.pauseTime();

#ifdef TIMERBASE
    double r_time=0.0;
    if(rank%2 == 1 || comm_size == 1)
        r_time=r.elapsed_time;
    double r_sum;
    MPI_Allreduce(&r_time, &r_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double r_mean = r_sum / comm_size*2;
    if(rank==0 && comm_size>1){
        stream <<r_mean<<",";
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
    global_timer.resumeTime();
    outfile=labios::fopen(finalname.c_str(), "w+");
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
    char final_buff[1024*1024];
    gen_random(final_buff,1024*1024);
    global_timer.resumeTime();
    labios::fwrite(final_buff, sizeof(char), 1024 * 1024, outfile);
    labios::fclose(outfile);
#ifdef TIMERBASE
    a.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    auto a_time=a.elapsed_time;
    double a_sum;
    MPI_Allreduce(&a_time, &a_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double a_mean = a_sum / comm_size;
    if(rank == 0) stream << a_mean<<",";
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
    auto bytes= labios::fread_wait(read_buf,tasks,filename);
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
    FILE* fh=labios::fopen(filename.c_str(),"w+");
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
    labios::fwrite(write_buf, sizeof(char), io_per_teration, fh);
    delete(write_buf);
    size_t count=0;
    std::vector<std::pair<size_t,std::vector<read_task>>> operations=
            std::vector<std::pair<size_t,std::vector<read_task>>>();
    MPI_Barrier(MPI_COMM_WORLD);
    if(rank == 0) std::cerr<<"Data created Done\n";
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
                    labios::fseek(fh,rand_offset,SEEK_SET);
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
                        labios::fread_async(sizeof(char), item[0],fh)));

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
    for(auto operation:operations){
        wait_for_read(operation.first, operation.second,filename);
    }
    labios::fclose(fh);
    MPI_Barrier(MPI_COMM_WORLD);
    if(rank == 0) std::cerr<<"Read Done\n";
#ifdef TIMERBASE
    map.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    auto map_time=map.elapsed_time;
    double map_sum;
    MPI_Allreduce(&map_time, &map_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double map_mean = map_sum / comm_size;
    if(rank == 0) stream << map_mean<<",";
#endif
    std::string finalname=pfs_path+"final_"+std::to_string(rank)+".dat";
    FILE* outfile;
    global_timer.resumeTime();
#ifdef TIMERBASE
    Timer reduce=Timer();
    reduce.resumeTime();
#endif
    outfile=labios::fopen(finalname.c_str(), "w+");
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
    labios::fwrite(out_buff, sizeof(char), 1024 * 1024, outfile);
    labios::fclose(outfile);
#ifdef TIMERBASE
    reduce.pauseTime();
#endif
    global_timer.pauseTime();
#ifdef TIMERBASE
    auto red_time=reduce.elapsed_time;
    double red_sum,max,min;
    MPI_Allreduce(&red_time, &red_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&red_time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&red_time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
    double red_mean = red_sum / comm_size;
    if(rank == 0) stream << red_mean<<","<<max<<","<<min;
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

void stress_test(int argc, char** argv){

    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    if(rank==0) std::cerr << "Stress test\n";
    const int io_size=1024*1024;
    int num_iterations=128;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=labios::fopen("file.test","w+");
    global_timer.pauseTime();
    std::vector<std::pair<size_t,std::vector<write_task*>>> operations=
            std::vector<std::pair<size_t,std::vector<write_task*>>>();
    char write_buf[io_size];
    gen_random(write_buf,io_size);
    for(int i=0;i<num_iterations;++i){
        global_timer.resumeTime();
        operations.emplace_back
                (std::make_pair(io_size,
                                labios::fwrite_async(write_buf,sizeof(char),
                                                     io_size,fh)));
        global_timer.pauseTime();
        if(i!=0 && i%32==0){
            if(rank==0) std::cerr << "Waiting..."<<i<<"\n";
            for(int task=0;task<32;++task){
                auto operation=operations[task];
                global_timer.resumeTime();
                auto bytes = labios::fwrite_wait(operation.second);
                if(bytes != operation.first) std::cerr << "Write failed\n";
                global_timer.pauseTime();
            }
            operations.erase(operations.begin(),operations.begin()+32);
            MPI_Barrier(MPI_COMM_WORLD);
        }
    }
    global_timer.resumeTime();
    for(auto operation:operations){
        auto bytes = labios::fwrite_wait(operation.second);
        if(bytes != operation.first) std::cerr << "Write failed\n";
    }
    global_timer.pauseTime();
    global_timer.resumeTime();
    labios::fclose(fh);
    global_timer.pauseTime();
    if(rank==0) std::cerr << "Done writing. Now reducing\n";
    auto time=global_timer.elapsed_time;
    double sum,max,min;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    MPI_Allreduce(&time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank==0){
        std::stringstream stream;
        std::cerr <<"Write finished\n";
        stream <<"mean,"<<mean<<"\tmax,"<<max<<"\tmin,"<<min<<"\n";
        std::cerr << stream.str();
    }
}


/******************************************************************************
*Main
******************************************************************************/
int main(int argc, char** argv){
    labios::MPI_Init(&argc,&argv);
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
#ifdef OVERHEADS
    std::string log_name=std::string(argv[0])+"_" +
                         std::to_string(rank) +".csv";
    freopen(log_name.c_str(), "w+", stdout);
//    if(rank==0 || rank==1){
//        std::string log_name=std::string(argv[0])+"_" +
//                             std::to_string(comm_size) +".csv";
//        freopen(log_name.c_str(), "w+", stdout);
//    }
#endif
    MPI_Barrier(MPI_COMM_WORLD);
    int return_val=0;
    if(argc > 1){
        testCase= static_cast<test_case>(atoi(argv[2]));
    }
    switch(testCase){
		case SIMPLE_WRITE:{
			FILE *fp;
			int rv; // return val
			char write_buf[50] = "Testing R/W with LABIOS. This is msg body.";

			Timer timer = Timer();
			timer.resumeTime();
			std::cerr << "This is a simple WRITE test.\n";

			// open/create file
			fp = labios::fopen(argv[2], "w+");
			if (fp == NULL) {
				std::cerr << "Failed to open/create file. Aborting...\n";
				exit(-1);
			}
				
			// write message to file
			rv = labios::fwrite(write_buf,sizeof(write_buf),1,fp);
			std::cerr << "(Return value: " << rv << ")\n";
			std::cerr << "Written to: " << argv[2] << "\n";

			labios::fclose(fp);
			timer.pauseTime();
			auto time = timer.elapsed_time;
			std::cerr << "Time elapsed: " << time << " seconds.\n";
			break;
		}
		case SIMPLE_READ:{
			FILE *fp;
			int rv; // return val
			char read_buf[50];
	
			Timer timer = Timer();
			timer.resumeTime();
			std::cerr << "This is a simple READ test.\n";
			
			// open file for reading
			fp = labios::fopen(argv[2], "rb");
			if (fp == NULL) {
				std::cerr << "Failed to find file. Aborting...\n";
				exit(-1);
			}

			// read
			rv = labios::fread(read_buf,sizeof(read_buf),1,fp);
			std::cerr << "(Return value: " << rv << ")\n";
			std::cerr << read_buf << "\n";

			labios::fclose(fp);
			timer.pauseTime();
			auto time = timer.elapsed_time;
			std::cerr << "Time elapsed: " << time << " seconds.\n";
			break;			
		}
        case CM1_BASE: {
#ifdef FTIMER
            Timer timer = Timer();
            timer.resumeTime();
            std::stringstream stream;
#endif
            cm1_base(argc, argv);
#ifdef FTIMER
            timer.pauseTime();
            auto time = timer.elapsed_time;
            double sum, max, min;
            MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            double mean = sum / comm_size;
            if(rank == 0) {
                stream << mean << "," << max << "," << min << "\n";
                std::cerr << stream.str();
            }
#endif
            break;
        }
        case CM1_TABIOS:{
#ifdef FTIMER
            Timer timer = Timer();
            timer.resumeTime();
            std::stringstream stream;
#endif
            cm1_tabios(argc,argv);
#ifdef FTIMER
            timer.pauseTime();
            auto time = timer.elapsed_time;
            double sum, max, min;
            MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            MPI_Allreduce(&time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            double mean = sum / comm_size;
            if(rank == 0) {
                stream << mean << "," << max << "," << min << "\n";
                std::cerr << stream.str();
            }
#endif
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
        case STRESS_TEST:{
            stress_test(argc,argv);
            break;
        }
    }

    labios::MPI_Finalize();

    return return_val;
}



