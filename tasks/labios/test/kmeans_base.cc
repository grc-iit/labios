//
// Created by lukemartinlogan on 7/22/22.
//

#include "hermes_shm/util/timer_mpi.h"
#include <fcntl.h>
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
    printf(
        "USAGE: ./kmeans_base [labios_conf] [file_path] [iter] [pfs_path]\n");
    exit(1);
  }

  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  stream << "average_kmeans_base," << std::fixed << std::setprecision(10);
  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string pfs_path = argv[4];
  std::string filename = file_path + "test_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({32 * 1024, 1024});
  size_t current_offset = 0;
  size_t align = 4096;
  hshm::MpiTimer mpi_timer(MPI_COMM_WORLD);

  mpi_timer.Resume();
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  int fd = open(filename.c_str(), O_RDWR | O_TRUNC | O_CREAT, mode);
  if (fd < 0) {
    perror("Failed to open file with O_CREAT");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  fd = open(filename.c_str(), O_DIRECT | O_RDWR | O_TRUNC, mode);
  if (fd < 0) {
    perror("Failed to open file with O_DIRECT");
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  mpi_timer.Pause();

  char *write_buf = new char[io_per_teration];
  // Function to generate random data
  gen_random(write_buf, io_per_teration);
  if (write(fd, write_buf, io_per_teration) < 0) {
    perror("Initial write failed");
  }
  if (fsync(fd) < 0) {
    perror("Initial fsync failed");
  }
  MPI_Barrier(MPI_COMM_WORLD);
  delete (write_buf);
  int count = 0;

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        if (count % 32 == 0) {
          std::random_device rd;
          std::mt19937 generator(rd());
          std::uniform_int_distribution<int> dist(0, 31);
          auto rand_offset = (dist(generator) * 1 * 1024 * 1024);
          mpi_timer.Resume();
          if (lseek(fd, rand_offset, SEEK_SET) < 0) {
            perror("lseek failed");
          }
          MPI_Barrier(MPI_COMM_WORLD);
          mpi_timer.Pause();
        }
        void *read_buf;
        read_buf = memalign(align * 2, item[0] + align);
        if (read_buf == NULL)
          std::cerr << "memalign\n";
        read_buf += align;
        mpi_timer.Resume();
        // auto bytes = std::fread(read_buf,sizeof(char),item[0],fh);

        auto bytes = read(fd, read_buf, item[0]);
        if (bytes < 0) {
          perror("read failed");
        } else if (bytes != item[0])
          std::cerr << "Read failed: wanted " << item[0] << " got " << bytes
                    << "\n";
        MPI_Barrier(MPI_COMM_WORLD);
        mpi_timer.Pause();
        current_offset += item[0];
        count++;
      }
    }
  }
  mpi_timer.Resume();
  // std::fclose(fh);
  close(fd);
  MPI_Barrier(MPI_COMM_WORLD);
  mpi_timer.Pause();
  MPI_File outFile;
  MPI_Info info;
  MPI_Info_create(&info);
  MPI_Info_set(info, "direct_write", "true");
  std::string final = pfs_path + "final.dat";
  mpi_timer.Resume();
  MPI_File_open(MPI_COMM_WORLD, final.c_str(), MPI_MODE_CREATE | MPI_MODE_RDWR,
                info, &outFile);
  MPI_File_set_view(outFile, static_cast<MPI_Offset>(rank * 1024 * 1024),
                    MPI_CHAR, MPI_CHAR, "native", MPI_INFO_NULL);
  mpi_timer.Pause();
  char out_buff[1024 * 1024];
  gen_random(out_buff, 1024 * 1024);
  mpi_timer.Resume();
  MPI_File_write(outFile, out_buff, (1024 * 1024), MPI_CHAR, MPI_STATUS_IGNORE);
  MPI_File_close(&outFile);
  mpi_timer.Pause();
  double avg = mpi_timer.CollectAvg().GetSec();
  if (rank == 0) {
    stream << "average," << avg << "\n";
    std::cerr << stream.str();
  }
  MPI_Finalize();
}