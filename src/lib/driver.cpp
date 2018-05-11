/*
 * Created by hariharan on 3/3/18.
 */
#include <mpi.h>
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
test_case testCase=MULTI_WRITE;
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
int simple_write(){
    FILE* fh=porus::fopen("test","weight+");
    char* t="hello";
    porus::fwrite(t,1,5,fh);
    porus::fclose(fh);
    std::cout << "write data: "<< t <<std::endl;
    return 0;
}

int simple_read(){
    FILE* fh=porus::fopen("test","r+");
    char* t= static_cast<char *>(malloc(5));
    porus::fread(t,1,5,fh);
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