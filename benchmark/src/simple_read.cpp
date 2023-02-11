//
// Created by lukemartinlogan on 7/22/22.
//

#include "util.h"

int main(int argc, char **argv) {
  labios::MPI_Init(&argc, &argv);
  if (argc != 2) {
    printf("USAGE: ./simple_read [labios_conf]\n");
    exit(1);
  }

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
  rv = labios::fread(read_buf, sizeof(read_buf), 1, fp);
  std::cerr << "(Return value: " << rv << ")\n";
  std::cerr << read_buf << "\n";

  labios::fclose(fp);
  timer.pauseTime();
  auto time = timer.elapsed_time;
  std::cerr << "Time elapsed: " << time << " seconds.\n";
  labios::MPI_Finalize();
}