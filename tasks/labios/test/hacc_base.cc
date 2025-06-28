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
  if (argc != 4) {
    printf("USAGE: ./hacc_base [file_path] [iteration] [buf_path]\n");
    exit(1);
  }

  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0)
    stream << "hacc_base()," << std::fixed << std::setprecision(10);
  std::string file_path = argv[1];
  int iteration = atoi(argv[2]);
  std::string buf_path = argv[3];
  std::string filename = buf_path + "test_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({1 * 1024 * 1024, 32});
  size_t current_offset = 0;
  hshm::MpiTimer mpi_timer(MPI_COMM_WORLD);
  char *write_buf[32];
  for (int i = 0; i < 32; ++i) {
    write_buf[i] = static_cast<char *>(malloc(1 * 1024 * 1024));
    gen_random(write_buf[i], 1 * 1024 * 1024);
  }
  mpi_timer.Resume();
  //    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  //    int fd=open(filename.c_str(),O_CREAT|O_SYNC|O_RSYNC|O_RDWR|O_TRUNC,
  //    mode);
  FILE *fh = std::fopen(filename.c_str(), "w+");
  if (fh == nullptr) {
    fprintf(stderr, "Failed to open file for writing: %s\\n", filename.c_str());
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  mpi_timer.Pause();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        mpi_timer.Resume();
        //                write(fd,write_buf,item[0]);
        //                fsync(fd);
        size_t bytes_written =
            std::fwrite(write_buf[j], sizeof(char), item[0], fh);
        if (bytes_written != item[0]) {
          fprintf(stderr, "Short write on %s\\n", filename.c_str());
        }
        MPI_Barrier(MPI_COMM_WORLD);
        mpi_timer.Pause();
        current_offset += item[0];
      }
    }
  }
  for (int i = 0; i < 32; ++i) {
    free(write_buf[i]);
  }
  auto read_buf = static_cast<char *>(calloc(io_per_teration, sizeof(char)));
  mpi_timer.Resume();

  // close(fd);
  std::fclose(fh);
  //    int in=open(filename.c_str(),O_SYNC|O_RSYNC|O_RDONLY| mode);
  //    read(in,read_buf,io_per_teration);
  //    close(in);
  FILE *fh1 = std::fopen(filename.c_str(), "r");
  if (fh1 == nullptr) {
    fprintf(stderr, "Failed to open file for reading: %s\\n", filename.c_str());
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  size_t bytes_read = std::fread(read_buf, sizeof(char), io_per_teration, fh1);
  if (bytes_read != io_per_teration) {
    fprintf(stderr, "Short read on %s. Expected %zu, got %zu\\n",
            filename.c_str(), io_per_teration, bytes_read);
  }
  std::fclose(fh1);
  MPI_Barrier(MPI_COMM_WORLD);

  std::string output = file_path + "final_" + std::to_string(rank) + ".out";
  //    int out=open(output.c_str(),O_CREAT|O_SYNC|O_WRONLY|O_TRUNC, mode);
  //    write(out,read_buf,io_per_teration);
  //    fsync(out);
  //    close(out);
  FILE *fh2 = std::fopen(output.c_str(), "w");
  if (fh2 == nullptr) {
    fprintf(stderr, "Failed to open file for final output: %s\\n",
            output.c_str());
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  std::fwrite(read_buf, sizeof(char), io_per_teration, fh2);
  std::fflush(fh2);
  std::fclose(fh2);
  MPI_Barrier(MPI_COMM_WORLD);
  mpi_timer.Pause();
  free(read_buf);
  double avg = mpi_timer.CollectAvg().GetSec();
  if (rank == 0) {
    stream << "average," << avg << "\\n";
    std::cerr << stream.str();
  }

  MPI_Finalize();
}