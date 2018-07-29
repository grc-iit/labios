/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include <random>
#include <iomanip>
#include <zconf.h>
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
test_case testCase=MULTI_WRITE;
/******************************************************************************
*Interface
******************************************************************************/
int simple_write();
int simple_read();
int multi_write();
int multi_read();

void montage_tabios(int argc, char **pString);

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
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"/test.dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 32});
    size_t current_offset=0;
    Timer global_timer=Timer();
    MPI_File outFile;
    global_timer.resumeTime();
    MPI_File_open(MPI_COMM_WORLD, filename.c_str(), MPI_MODE_CREATE | MPI_MODE_WRONLY, MPI_INFO_NULL, &outFile);
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                MPI_File_write_at_all(outFile, static_cast<MPI_Offset>(rank * io_per_teration + current_offset), write_buf,
                                      static_cast<int>(write[0]), MPI_CHAR, MPI_STATUS_IGNORE);
                global_timer.pauseTime();
                current_offset+=write[0];
            }
        }
    }
    global_timer.resumeTime();
    MPI_File_close(&outFile);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        printf("Time : %lf\n",mean);
        std::cout << "MPIIOSHARED,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}

void cm1_tabios(int argc, char** argv) {
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
                //usleep(1000000);
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
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}


void montage_base(int argc, char** argv) {
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({1*1024*1024, 16});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=std::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                std::fwrite(write_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
                //usleep(1000000);
            }
        }
    }
    fseek(fh,0,SEEK_SET);
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char read_buf[write[0]];
                global_timer.resumeTime();
                std::fread(read_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
                //usleep(1000000);
            }
        }
    }
    global_timer.resumeTime();
    std::fclose(fh);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}

void hacc_base(int argc, char** argv) {
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
    FILE* fh=std::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char write_buf[write[0]];
                gen_random(write_buf,write[0]);
                global_timer.resumeTime();
                std::fwrite(write_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
                //usleep(1000000);
            }
        }
    }

    global_timer.resumeTime();
    std::fclose(fh);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}

void kmeans_base(int argc, char** argv) {
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({16*1024, 2048});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=std::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    std::fwrite(write_buf,sizeof(char),io_per_teration,fh);
    delete(write_buf);
    for(int i=0;i<iteration;++i){
        for(auto read:workload){
            for(int j=0;j<read[1];++j){
                int rand_offset = static_cast<int>(rand() % (io_per_teration - 32 * 1024));
                char read_buf[read[0]];
                global_timer.resumeTime();
                std::fseek(fh,rand_offset,SEEK_SET);
                std::fread(read_buf,sizeof(char),read[0],fh);
                global_timer.pauseTime();
                current_offset+=read[0];
            }
        }
    }

    global_timer.resumeTime();
    std::fclose(fh);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}

void hacc_tabios(int argc, char **argv) {
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

    global_timer.resumeTime();
    aetrio::fclose(fh);
    global_timer.pauseTime();
    auto time=global_timer.elapsed_time;
    double sum;
    MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    double mean = sum / comm_size;
    if(rank == 0) {
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }

}

void kmeans_tabios(int argc, char **argv) {
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string file_path=argv[2];
    int iteration=atoi(argv[3]);
    parse_opts(argc,argv);
    std::string filename=file_path+"/test_"+std::to_string(rank)+".dat";
    size_t io_per_teration=32*1024*1024;
    std::vector<std::array<size_t,2>> workload=std::vector<std::array<size_t,2>>();
    workload.push_back({16*1024, 2048});

    size_t current_offset=0;
    Timer global_timer=Timer();
    global_timer.resumeTime();
    FILE* fh=aetrio::fopen(filename.c_str(),"w+");
    global_timer.pauseTime();
    char* write_buf=new char[io_per_teration];
    gen_random(write_buf,io_per_teration);
    aetrio::fwrite(write_buf,sizeof(char),io_per_teration,fh);
    delete(write_buf);
    size_t count=0;
    for(int i=0;i<iteration;++i){
        for(auto read:workload){
            for(int j=0;j<read[1];++j){
                size_t rand_offset =rand() % (io_per_teration - 32 * 1024);
                if(count%50==0)
                std::cout<<count<<" read: offset: "<<rand_offset<<" size: "
                                                          ""<<read[0]<<"\n";
                char read_buf[read[0]];
                global_timer.resumeTime();
                aetrio::fseek(fh,rand_offset,SEEK_SET);
                aetrio::fread(read_buf,sizeof(char),read[0],fh);
                global_timer.pauseTime();
                current_offset+=read[0];
                count++;

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
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}

/******************************************************************************
*Main
******************************************************************************/
int main(int argc, char** argv){
    aetrio::MPI_Init(&argc,&argv);
    int rank,comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    std::string log_name=std::string(argv[0])+"_"+std::to_string(rank)+".log";
    freopen(log_name.c_str(), "w", stdout);
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

void montage_tabios(int argc, char **argv) {
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
                //usleep(1000000);
            }
        }
    }
    aetrio::fseek(fh,0,SEEK_SET);
    for(int i=0;i<iteration;++i){
        for(auto write:workload){
            for(int j=0;j<write[1];++j){
                char read_buf[write[0]];
                global_timer.resumeTime();
                aetrio::fread(read_buf,sizeof(char),write[0],fh);
                global_timer.pauseTime();
                current_offset+=write[0];
                //usleep(1000000);
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
        printf("Time : %lf\n",mean);
        std::cout << "TABIOS,"
                  << "average,"
                  << std::setprecision(6)
                  << mean
                  << "\n";
    }
}

int simple_write(){
    FILE* fh=aetrio::fopen("test","w+");
    size_t size_of_io=512 * 1024 * 1024;
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

    size_t size_of_io=4 * 1024 * 1024;
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

