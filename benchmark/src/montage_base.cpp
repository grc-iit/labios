//
// Created by lukemartinlogan on 7/22/22.
//

#include "util.h"

int main(int argc, char **argv) {
  labios::MPI_Init(&argc, &argv);
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
#ifdef TIMERBASE
  Timer c = Timer();
  c.resumeTime();
#endif
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
#ifdef TIMERBASE
  if (rank == 0) {
    stream << "montage_base()," << std::fixed << std::setprecision(10)
           << c.pauseTime() << ",";
  }
#endif
  Timer global_timer = Timer();
  global_timer.resumeTime();
#ifdef TIMERBASE
  Timer w = Timer();
  w.resumeTime();
#endif
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  int fd1, fd2;
  if (rank % 2 == 0) {
    fd1 = open(filename1.c_str(),
               O_CREAT | O_SYNC | O_DSYNC | O_WRONLY | O_TRUNC, mode);
    fd2 = open(filename2.c_str(),
               O_CREAT | O_SYNC | O_DSYNC | O_WRONLY | O_TRUNC, mode);
  }
#ifdef TIMERBASE
  w.pauseTime();
#endif
  global_timer.pauseTime();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        global_timer.resumeTime();
#ifdef TIMERBASE
        w.resumeTime();
#endif
        if (rank % 2 == 0) {
          if (j % 2 == 0) {
            write(fd1, write_buf[j], item[0]);
            fsync(fd1);
          } else {
            write(fd2, write_buf[j], item[0]);
            fsync(fd2);
          }
        }
        MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
        w.pauseTime();
#endif
        global_timer.pauseTime();
        current_offset += item[0];
      }
    }
  }
  for (int i = 0; i < 32; ++i) {
    free(write_buf[i]);
  }
  global_timer.resumeTime();
#ifdef TIMERBASE
  w.resumeTime();
#endif
  if (rank % 2 == 0) {
    close(fd1);
    close(fd2);
  }
  MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
  w.pauseTime();
#endif
  global_timer.pauseTime();
#ifdef TIMERBASE
  if (rank == 0)
    stream << w.elapsed_time << ",";
#endif

#ifdef TIMERBASE
  Timer r = Timer();
  r.resumeTime();
#endif
  size_t align = 4096;
  global_timer.resumeTime();
  if (rank % 2 != 0) {
    filename1 = file_path + "file1_" + std::to_string(rank - 1) + ".dat";
    filename2 = file_path + "file2_" + std::to_string(rank - 1) + ".dat";
    fd1 = open(filename1.c_str(), O_DIRECT | O_RDONLY | mode);
    if (fd1 == -1)
      std::cerr << "open() failed!\n";
    fd2 = open(filename2.c_str(), O_DIRECT | O_RDONLY | mode);
    if (fd2 == -1)
      std::cerr << "open() failed!\n";
  }
#ifdef TIMERBASE
  r.pauseTime();
#endif
  global_timer.pauseTime();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        void *read_buf;
        read_buf = memalign(align * 2, item[0] + align);
        if (read_buf == NULL)
          std::cerr << "memalign\n";
        read_buf += align;
        global_timer.resumeTime();
#ifdef TIMERBASE
        r.resumeTime();
#endif
        if (rank % 2 != 0) {
          ssize_t bytes = 0;
          bytes = read(fd1, read_buf, item[0] / 2);
          bytes += read(fd2, read_buf, item[0] / 2);
          if (bytes != item[0])
            std::cerr << "Read() failed!"
                      << "Bytes:" << bytes << "\tError code:" << errno << "\n";
        }
        MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
        r.pauseTime();
#endif
        global_timer.pauseTime();
        current_offset += item[0];
      }
    }
  }
  global_timer.resumeTime();
#ifdef TIMERBASE
  r.resumeTime();
#endif
  if (rank % 2 != 0) {
    close(fd1);
    close(fd2);
  }
#ifdef TIMERBASE
  r.pauseTime();
#endif

#ifdef TIMERBASE
  if (rank == 0)
    stream << r.elapsed_time << ",";
#endif

#ifdef TIMERBASE
  Timer a = Timer();
  a.resumeTime();
#endif
  std::string finalname = final_path + "final_" + std::to_string(rank) + ".dat";
  std::fstream outfile;
  outfile.open(finalname, std::ios::out);
  global_timer.pauseTime();
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
  global_timer.resumeTime();
  outfile << buff << std::endl;
  outfile.close();
  global_timer.pauseTime();

#ifdef TIMERBASE
  if (rank == 0)
    stream << a.pauseTime() << ",";
#endif

  auto time = global_timer.elapsed_time;
  double sum;
  MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double mean = sum / comm_size;
  if (rank == 0) {
    stream << "average," << mean << "\n";
    std::cerr << stream.str();
  }
  labios::MPI_Finalize();
}