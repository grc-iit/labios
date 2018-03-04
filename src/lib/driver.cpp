#include <mpi.h>
#include "../../include/posix.h"

//
// Created by hariharan on 3/3/18.
//
int main(int argc, char** argv){
    porus::MPI_Init(&argc,&argv);
    FILE* fh=porus::fopen("test","w+");
    char* t="hello";
    porus::fwrite(t,1,5,fh);
    porus::fclose(fh);
    porus::MPI_Finalize();
}

