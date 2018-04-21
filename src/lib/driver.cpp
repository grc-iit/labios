/*
 * Created by hariharan on 3/3/18.
 */
#include <mpi.h>
#include "../../include/posix.h"
enum test_case{
    SIMPLE_WRITE=0,
    SIMPLE_READ=1,
    MULTI_WRITE=2,
    MULTI_READ=3

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
        case MULTI_WRITE:{
            return_val=multi_write();
            break;
        }
        case MULTI_READ:{
            return_val=multi_read();
            break;
        }
    }
    porus::MPI_Finalize();
    return return_val;
}
int simple_write(){
    FILE* fh=porus::fopen("test","w+");
    char* t="hello";
    porus::fwrite(t,1,5,fh);
    porus::fclose(fh);
    return 0;
}

int simple_read(){
    FILE* fh=porus::fopen("test","r+");
    char* t= static_cast<char *>(malloc(5));
    porus::fread(t,1,5,fh);
    porus::fclose(fh);
    return 0;
}
int multi_write(){
    FILE* fh=porus::fopen("test","w+");
    char* t="hello";
    porus::fwrite(t,1,5,fh);
    porus::fclose(fh);
    return 0;
}
int multi_read(){
    FILE* fh=porus::fopen("test","r+");
    char* t= static_cast<char *>(malloc(5));
    porus::fread(t,1,5,fh);
    porus::fclose(fh);
    return 0;
}