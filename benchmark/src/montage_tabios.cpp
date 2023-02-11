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

#ifdef COLLECT
  system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#ifdef DEBUG
  if (rank == 0)
    std::cerr << "Running Montage in TABIOS\n";
#endif
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string final_path = argv[4];
  std::string filename1 = file_path + "file1_" + std::to_string(rank) + ".dat";
  std::string filename2 = file_path + "file2_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
  Timer c = Timer();
  c.resumeTime();
#endif
#ifdef DEBUG
  if (rank == 0)
    std::cerr << "Starting Simulation Phase\n";
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
  c.pauseTime();
#endif
#ifdef TIMERBASE
  if (rank == 0) {
    stream << "montage_tabios()," << std::fixed << std::setprecision(10)
           << c.elapsed_time << ",";
  }
#endif
#ifdef TIMERBASE
  Timer w = Timer();
#endif
#ifdef DEBUG
  if (rank == 0)
    std::cerr << "Starting Write Phase\n";
#endif
  Timer global_timer = Timer();
  global_timer.resumeTime();
  FILE *fd1, *fd2;
  if (rank % 2 == 0 || comm_size == 1) {
#ifdef TIMERBASE
    w.resumeTime();
#endif
    fd1 = labios::fopen(filename1.c_str(), "w+");
    fd2 = labios::fopen(filename2.c_str(), "w+");
#ifdef TIMERBASE
    w.pauseTime();
#endif
  }
  global_timer.pauseTime();
  std::vector<std::pair<size_t, std::vector<write_task *>>> operations =
      std::vector<std::pair<size_t, std::vector<write_task *>>>();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        global_timer.resumeTime();
#ifdef TIMERBASE
        w.resumeTime();
#endif
        if (rank % 2 == 0 || comm_size == 1) {
          if (j % 2 == 0) {
            operations.emplace_back(std::make_pair(
                item[0], labios::fwrite_async(write_buf[j], sizeof(char),
                                              item[0], fd1)));
          } else {
            operations.emplace_back(std::make_pair(
                item[0], labios::fwrite_async(write_buf[j], sizeof(char),
                                              item[0], fd2)));
          }
        }
#ifdef TIMERBASE
        w.pauseTime();
#endif
        global_timer.pauseTime();
        current_offset += item[0];
      }
    }
  }
  global_timer.resumeTime();
#ifdef TIMERBASE
  w.resumeTime();
#endif
  if (rank % 2 == 0 || comm_size == 1) {
    for (auto operation : operations) {
      auto bytes = labios::fwrite_wait(operation.second);
      if (bytes != operation.first)
        std::cerr << "Write failed\n";
    }
    labios::fclose(fd1);
    labios::fclose(fd2);
  }
#ifdef TIMERBASE
  w.pauseTime();
#endif
  global_timer.pauseTime();
  MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE

  double w_time = 0.0;
  if (rank % 2 == 0)
    w_time = w.elapsed_time;
  double w_sum;
  MPI_Allreduce(&w_time, &w_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double w_mean = w_sum / comm_size * 2;
  if (rank == 0)
    stream << w_mean << ",";
#endif
  for (int i = 0; i < 32; ++i) {
    free(write_buf[i]);
  }
#ifdef TIMERBASE
  Timer r = Timer();
#endif
#ifdef DEBUG
  if (rank == 0)
    std::cerr << "Starting Reading Phase\n";
#endif
  size_t align = 4096;
  global_timer.resumeTime();
#ifdef TIMERBASE
  r.resumeTime();
#endif
  if (rank % 2 != 0 || comm_size == 1) {
    if (comm_size == 1) {
      filename1 = file_path + "file1_" + std::to_string(rank) + ".dat";
      filename2 = file_path + "file2_" + std::to_string(rank) + ".dat";
    } else {
      filename1 = file_path + "file1_" + std::to_string(rank - 1) + ".dat";
      filename2 = file_path + "file2_" + std::to_string(rank - 1) + ".dat";
    }

    fd1 = labios::fopen(filename1.c_str(), "r+");
    fd2 = labios::fopen(filename2.c_str(), "r+");
  }
#ifdef TIMERBASE
  r.pauseTime();
#endif
  global_timer.pauseTime();
  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        char read_buf[item[0]];
        global_timer.resumeTime();

#ifdef TIMERBASE
        r.resumeTime();
#endif
        if (rank % 2 != 0 || comm_size == 1) {
          ssize_t bytes = 0;
#ifndef COLLECT
          bytes = labios::fread(read_buf, sizeof(char), item[0] / 2, fd1);
          bytes += labios::fread(read_buf, sizeof(char), item[0] / 2, fd2);
          if (bytes != item[0])
            std::cerr << "Read() failed!"
                      << "Bytes:" << bytes << "\tError code:" << errno << "\n";
#endif
        }
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
  if (rank % 2 != 0 || comm_size == 1) {
    labios::fclose(fd1);
    labios::fclose(fd2);
  }
#ifdef TIMERBASE
  r.pauseTime();
#endif
  MPI_Barrier(MPI_COMM_WORLD);
  global_timer.pauseTime();

#ifdef TIMERBASE
  double r_time = 0.0;
  if (rank % 2 == 1 || comm_size == 1)
    r_time = r.elapsed_time;
  double r_sum;
  MPI_Allreduce(&r_time, &r_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double r_mean = r_sum / comm_size * 2;
  if (rank == 0 && comm_size > 1) {
    stream << r_mean << ",";
  }
#endif
#ifdef DEBUG
  if (rank == 0)
    std::cerr << "Starting Analysis Phase\n";
#endif
#ifdef TIMERBASE
  Timer a = Timer();
  a.resumeTime();
#endif
  std::string finalname = final_path + "final_" + std::to_string(rank) + ".dat";
  FILE *outfile;
  global_timer.resumeTime();
  outfile = labios::fopen(finalname.c_str(), "w+");
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
  char final_buff[1024 * 1024];
  gen_random(final_buff, 1024 * 1024);
  global_timer.resumeTime();
  labios::fwrite(final_buff, sizeof(char), 1024 * 1024, outfile);
  labios::fclose(outfile);
#ifdef TIMERBASE
  a.pauseTime();
#endif
  global_timer.pauseTime();
#ifdef TIMERBASE
  auto a_time = a.elapsed_time;
  double a_sum;
  MPI_Allreduce(&a_time, &a_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double a_mean = a_sum / comm_size;
  if (rank == 0)
    stream << a_mean << ",";
#endif

  auto time = global_timer.elapsed_time;
  double sum;
  MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double mean = sum / comm_size;
  if (rank == 0) {
#ifdef COLLECT
    double ts = get_average_ts();
    double worker = get_average_worker();
    stream << ts << "," << worker << ",";
#endif
    stream << "average," << mean << "\n";
    std::cerr << stream.str();
  }
  labios::MPI_Finalize();
}