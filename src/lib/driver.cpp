/******************************************************************************
*include files
******************************************************************************/
#include <mpi.h>
#include <zconf.h>
#include <random>
#include "posix.h"
#include "../common/return_codes.h"

enum test_case{
    SIMPLE_WRITE=0,
    SIMPLE_READ=1,
    MULTI_WRITE=2,
    MULTI_READ=3,
    SIMPLE_MIXED=4,
    MULTI_MIXED=5
};
/*
 * set test case
 */
test_case testCase=MULTI_WRITE;
/******************************************************************************
*Interface
******************************************************************************/
/*
 * function definitions
 */
int simple_write();
int simple_read();
int multi_write();
int multi_read();



int main(int argc, char** argv){

    aetrio::MPI_Init(&argc,&argv);
    int return_val=0;
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
            sleep(2);
            return_val=simple_read();
//            sleep(2);
//            return_val=simple_write();
//            sleep(2);
//            return_val=simple_read();
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
    }
    aetrio::MPI_Finalize();

    return return_val;
}
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

