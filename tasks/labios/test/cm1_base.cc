//
// Created by lukemartinlogan on 7/22/22.
//

#include "hermes_shm/util/timer_mpi.h"
#include <iomanip>
#include <mpi.h>
#include <random>
#include <sstream>

void gen_random(char *buf, size_t size) {
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<int> dist(0, 255);
  for (size_t i = 0; i < size; ++i) {
    buf[i] = static_cast<char>(dist(generator));
  }
}
int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  if (argc != 3) {
    printf("USAGE: ./cm1_base [file_path] [iteration]\n");
    exit(1);
  }

  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  std::string file_path = argv[1];
  int iteration = atoi(argv[2]);

  std::string filename = file_path + "test.dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({1 * 1024 * 1024, 32});
  size_t current_offset = 0;

  hshm::MpiTimer mpi_timer(MPI_COMM_WORLD);
  MPI_File outFile;
  char *write_buf[32];
  for (int i = 0; i < 32; ++i) {
    write_buf[i] = static_cast<char *>(malloc(1 * 1024 * 1024));
    gen_random(write_buf[i], 1 * 1024 * 1024);
  }
  mpi_timer.Resume();
  MPI_Info info;
  MPI_Info_create(&info);
  MPI_Info_set(info, "direct_write", "true");

  int ret = MPI_File_open(MPI_COMM_WORLD, filename.c_str(),
                          MPI_MODE_CREATE | MPI_MODE_RDWR, info, &outFile);
  if (ret != MPI_SUCCESS) {
    fprintf(stderr, "Failed to open file %s\n", filename.c_str());
    MPI_Abort(MPI_COMM_WORLD, ret);
  }
  MPI_File_set_view(outFile, static_cast<MPI_Offset>(rank * io_per_teration),
                    MPI_CHAR, MPI_CHAR, "native", MPI_INFO_NULL);
  mpi_timer.Pause();
  for (int i = 0; i < iteration; ++i) {
    for (auto write : workload) {
      for (int j = 0; j < write[1]; ++j) {
        mpi_timer.Resume();
        MPI_File_write(outFile, write_buf[j], static_cast<int>(write[0]),
                       MPI_CHAR, MPI_STATUS_IGNORE);
        MPI_Barrier(MPI_COMM_WORLD);
        mpi_timer.Pause();
        current_offset += write[0];
      }
    }
  }
  for (int i = 0; i < 32; ++i) {
    free(write_buf[i]);
  }
  mpi_timer.Resume();
  MPI_File_close(&outFile);
  MPI_Barrier(MPI_COMM_WORLD);
  mpi_timer.Pause();
  double avg = mpi_timer.CollectAvg().GetSec();
  if (rank == 0) {
    stream << "cm1_base," << std::fixed << std::setprecision(6) << avg << "\n";
    std::cerr << stream.str();
  }

  MPI_Finalize();
}