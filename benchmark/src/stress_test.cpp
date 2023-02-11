//
// Created by lukemartinlogan on 7/22/22.
//

#include "util.h"

int main(int argc, char **argv) {
  labios::MPI_Init(&argc, &argv);
  if (argc != 2) {
    printf("USAGE: ./stress_test [labios_conf]\n");
    exit(1);
  }

  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  if (rank == 0)
    std::cerr << "Stress test\n";
  const int io_size = 1024 * 1024;
  int num_iterations = 128;
  Timer global_timer = Timer();
  global_timer.resumeTime();
  FILE *fh = labios::fopen("file.test", "w+");
  global_timer.pauseTime();
  std::vector<std::pair<size_t, std::vector<write_task *>>> operations =
      std::vector<std::pair<size_t, std::vector<write_task *>>>();
  char write_buf[io_size];
  gen_random(write_buf, io_size);
  for (int i = 0; i < num_iterations; ++i) {
    global_timer.resumeTime();
    operations.emplace_back(std::make_pair(
        io_size, labios::fwrite_async(write_buf, sizeof(char), io_size, fh)));
    global_timer.pauseTime();
    if (i != 0 && i % 32 == 0) {
      if (rank == 0)
        std::cerr << "Waiting..." << i << "\n";
      for (int task = 0; task < 32; ++task) {
        auto operation = operations[task];
        global_timer.resumeTime();
        auto bytes = labios::fwrite_wait(operation.second);
        if (bytes != operation.first)
          std::cerr << "Write failed\n";
        global_timer.pauseTime();
      }
      operations.erase(operations.begin(), operations.begin() + 32);
      MPI_Barrier(MPI_COMM_WORLD);
    }
  }
  global_timer.resumeTime();
  for (auto operation : operations) {
    auto bytes = labios::fwrite_wait(operation.second);
    if (bytes != operation.first)
      std::cerr << "Write failed\n";
  }
  global_timer.pauseTime();
  global_timer.resumeTime();
  labios::fclose(fh);
  global_timer.pauseTime();
  if (rank == 0)
    std::cerr << "Done writing. Now reducing\n";
  auto time = global_timer.elapsed_time;
  double sum, max, min;
  MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
  double mean = sum / comm_size;
  if (rank == 0) {
    std::stringstream stream;
    std::cerr << "Write finished\n";
    stream << "mean," << mean << "\tmax," << max << "\tmin," << min << "\n";
    std::cerr << stream.str();
  }
  labios::MPI_Finalize();
}