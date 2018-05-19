/*
 * Created by hariharan on 3/3/18.
 */
#include <mpi.h>
#include <zconf.h>
#include "posix.h"
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
test_case testCase=SIMPLE_WRITE;
/*
 * function definitions
 */
int simple_write();
int simple_read();
int multi_write();
int multi_read();
int main(int argc, char** argv){
    porus::MPI_Init(&argc,&argv);
    int return_val;
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
            sleep(5);
            return_val=simple_write();
            //sleep(2);
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
            return_val=multi_read();
            break;
        }

    }
    porus::MPI_Finalize();
    return return_val;
}
void gen_random(char *s, const int len) {
    static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < len; ++i) {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    s[len] = 0;
}
int simple_write(){
    FILE* fh=porus::fopen("test","w+");
    size_t size_of_io=16 * 1024 * 1024;
    char* t= static_cast<char *>(malloc(size_of_io));
    gen_random(t,size_of_io);
    porus::fwrite(t,1,size_of_io,fh);
    porus::fclose(fh);
    //std::cout << "write data: "<< t <<std::endl;
    return 0;
}

int simple_read(){
    FILE* fh=porus::fopen("test","r+");
    size_t size_of_io=4 * 1024 * 1024;
    char* t= static_cast<char *>(malloc(size_of_io));
    porus::fread(t,1,size_of_io,fh);
    std::cout << "read data: "<< t <<std::endl;
    porus::fclose(fh);
    return 0;
}
int multi_write(){
    FILE* fh=porus::fopen("test","weight+");
    size_t size_of_io=16 * 1024 * 1024;
    char* t= static_cast<char *>(calloc(size_of_io, 1));
    for(int i=0;i<1024;i++){
        porus::fwrite(t,1,size_of_io,fh);
    }
    porus::fclose(fh);
    return 0;
}
int multi_read(){
    FILE* fh=porus::fopen("test","r+");
    size_t size_of_io=16 * 1024 * 1024;
    char* t= static_cast<char *>(malloc(size_of_io));
    for(int i=0;i<1024;i++){
        porus::fwrite(t,1,size_of_io,fh);
    }
    porus::fclose(fh);
    return 0;
}
