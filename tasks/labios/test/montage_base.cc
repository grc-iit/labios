//
// Created by lukemartinlogan on 7/22/22.
//

#include "hermes_shm/util/timer_mpi.h"
#include <fcntl.h>
#include <fstream> // Include fstream header
#include <iomanip>
#include <malloc.h>
#include <mpi.h>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

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
  if (argc != 5) {
    printf("USAGE: ./montage_base [labios_conf] [file_path] [iter] "
           "[final_path]\n");
    exit(1);
  }

  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string final_path = argv[4];
  std::string filename1 = file_path + "file1_" + std::to_string(rank) + ".dat";
  std::string filename2 = file_path + "file2_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  int count = 0;
  for (auto i = 0; i < 32; ++i) {
    for (int j = 0; j < comm_size * 1024 * 128; ++j) {
      count += 1;
      auto result = count * j;
      result -= j;
    }
    count = 0;
  }
  workload.push_back({1 * 1024 * 1024, 32});
  size_t current_offset = 0;
  char *write_buf[32];
  for (int i = 0; i < 32; ++i) {
    write_buf[i] = static_cast<char *>(malloc(1 * 1024 * 1024));
    gen_random(write_buf[i], 1 * 1024 * 1024);
  }
  hshm::MpiTimer mpi_timer(MPI_COMM_WORLD);
  mpi_timer.Resume();
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  int fd1 = -1, fd2 = -1;
  if (rank % 2 == 0) {
    fd1 = open(filename1.c_str(),
               O_CREAT | O_SYNC | O_DSYNC | O_WRONLY | O_TRUNC, mode);
    if (fd1 < 0) {
      perror("Failed to open file1 for writing");
      MPI_Abort(MPI_COMM_WORLD, 1);
    }
    fd2 = open(filename2.c_str(),
               O_CREAT | O_SYNC | O_DSYNC | O_WRONLY | O_TRUNC, mode);
    if (fd2 < 0) {
      perror("Failed to open file2 for writing");
      MPI_Abort(MPI_COMM_WORLD, 1);
    }
  }
  mpi_timer.Pause();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        mpi_timer.Resume();
        if (rank % 2 == 0) {
          if (j % 2 == 0) {
            if (write(fd1, write_buf[j], item[0]) < 0)
              perror("write failed to fd1");
            if (fsync(fd1) < 0)
              perror("fsync failed on fd1");
          } else {
            if (write(fd2, write_buf[j], item[0]) < 0)
              perror("write failed to fd2");
            if (fsync(fd2) < 0)
              perror("fsync failed on fd2");
          }
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
  mpi_timer.Resume();
  if (rank % 2 == 0) {
    if (fd1 >= 0)
      close(fd1);
    if (fd2 >= 0)
      close(fd2);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  mpi_timer.Pause();

  size_t align = 4096;
  mpi_timer.Resume();
  if (rank % 2 != 0) {
    filename1 = file_path + "file1_" + std::to_string(rank - 1) + ".dat";
    filename2 = file_path + "file2_" + std::to_string(rank - 1) + ".dat";
    fd1 = open(filename1.c_str(), O_DIRECT | O_RDONLY | mode);
    if (fd1 < 0) {
      perror("open() failed for file1 read");
      std::cerr << "Filename: " << filename1 << std::endl;
    }
    fd2 = open(filename2.c_str(), O_DIRECT | O_RDONLY | mode);
    if (fd2 < 0) {
      perror("open() failed for file2 read");
      std::cerr << "Filename: " << filename2 << std::endl;
    }
  }
  mpi_timer.Pause();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        void *read_buf;
        read_buf = memalign(align * 2, item[0] + align);
        if (read_buf == NULL)
          std::cerr << "memalign\n";
        read_buf += align;
        mpi_timer.Resume();
        if (rank % 2 != 0) {
          ssize_t bytes = 0;
          if (fd1 >= 0)
            bytes += read(fd1, read_buf, item[0] / 2);
          if (fd2 >= 0)
            bytes += read(fd2, read_buf, item[0] / 2);
          if (bytes < 0) {
            perror("Read() failed!");
          } else if (bytes != item[0])
            std::cerr << "Read() failed! Wanted " << item[0] << " Got " << bytes
                      << "\n";
        }
        MPI_Barrier(MPI_COMM_WORLD);
        mpi_timer.Pause();
        current_offset += item[0];
      }
    }
  }
  mpi_timer.Resume();
  if (rank % 2 != 0) {
    if (fd1 >= 0)
      close(fd1);
    if (fd2 >= 0)
      close(fd2);
  }

  std::string finalname = final_path + "final_" + std::to_string(rank) + ".dat";
  std::fstream outfile;
  outfile.open(finalname, std::ios::out);
  if (!outfile.is_open()) {
    std::cerr << "Failed to open final output file: " << finalname << std::endl;
  }
  mpi_timer.Pause();
  for (auto i = 0; i < 32; ++i) {
    for (int j = 0; j < comm_size * 1024 * 128; ++j) {
      count += 1;
      auto result = count * j;
      result -= j;
    }
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> dist(0, io_per_teration);
    auto rand = dist(generator);
    int x = (i + 1) * rand;
  }
  char buff[1024 * 1024];
  gen_random(buff, 1024 * 1024);
  mpi_timer.Resume();
  outfile << buff << std::endl;
  outfile.close();
  mpi_timer.Pause();

  double avg = mpi_timer.CollectAvg().GetSec();
  if (rank == 0) {
    stream << "average," << avg << "\n";
    std::cerr << stream.str();
  }
  MPI_Finalize();
}