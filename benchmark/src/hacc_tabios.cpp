//
// Created by lukemartinlogan on 7/22/22.
//

#include "util.h"

int main(int argc, char **argv) {
  labios::MPI_Init(&argc, &argv);
  if (argc != 5) {
    printf("USAGE: ./hacc_tabios [labios_conf] [file_path] [iteration] "
           "[buf_path]\n");
    exit(1);
  }

  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
#ifdef COLLECT
  if (rank == 0)
    system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
  if (rank == 0)
    stream << "hacc_tabios()" << std::fixed << std::setprecision(10);
  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string buf_path = argv[4];
  std::string filename = buf_path + "test_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({1 * 1024 * 1024, 32});
  size_t current_offset = 0;
  Timer global_timer = Timer();
  char *write_buf[32];
  for (int i = 0; i < 32; ++i) {
    write_buf[i] = static_cast<char *>(malloc(1 * 1024 * 1024));
    gen_random(write_buf[i], 1 * 1024 * 1024);
  }
  global_timer.resumeTime();
#ifdef TIMERBASE
  Timer wbb = Timer();
  wbb.resumeTime();
#endif
  FILE *fh = labios::fopen(filename.c_str(), "w+");
#ifdef TIMERBASE
  wbb.pauseTime();
#endif
  global_timer.pauseTime();

  std::vector<std::pair<size_t, std::vector<write_task *>>> operations =
      std::vector<std::pair<size_t, std::vector<write_task *>>>();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        global_timer.resumeTime();
#ifdef TIMERBASE
        wbb.resumeTime();
#endif
        operations.emplace_back(std::make_pair(
            item[0],
            labios::fwrite_async(write_buf[j], sizeof(char), item[0], fh)));
#ifdef TIMERBASE
        wbb.pauseTime();
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
  wbb.resumeTime();
#endif
  for (auto operation : operations) {
    auto bytes = labios::fwrite_wait(operation.second);
    if (bytes != operation.first)
      std::cerr << "Write failed\n";
  }
#ifdef TIMERBASE
  wbb.pauseTime();
#endif
#ifdef TIMERBASE
  auto writeBB = wbb.elapsed_time;
  double bb_sum;
  MPI_Allreduce(&writeBB, &bb_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double bb_mean = bb_sum / comm_size;
  if (rank == 0)
    stream << "write_to_BB," << bb_mean << ",";
#endif

  char *read_buf = (char *)malloc(io_per_teration * sizeof(char));
  gen_random(read_buf, io_per_teration);
  global_timer.resumeTime();
#ifdef TIMERBASE
  Timer rbb = Timer();
  rbb.resumeTime();
#endif
  labios::fclose(fh);

#ifndef COLLECT
  FILE *fh1 = labios::fopen(filename.c_str(), "r+");
  auto op = labios::fread_async(sizeof(char), io_per_teration, fh1);
  //    auto bytes= labios::fread_wait(read_buf,op,filename);
  //    if(bytes!=io_per_teration) std::cerr << "Read failed:" <<bytes<<"\n";
  labios::fclose(fh1);

#endif

#ifdef TIMERBASE
  rbb.pauseTime();
  auto read_time = rbb.elapsed_time;
  double read_sum;
  MPI_Allreduce(&read_time, &read_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double read_mean = read_sum / comm_size;
  if (rank == 0)
    stream << "read_from_BB," << read_mean << ",";
#endif

  std::string output = file_path + "final_" + std::to_string(rank) + ".out";
#ifdef TIMERBASE
  Timer pfs = Timer();
  pfs.resumeTime();
#endif
  FILE *fh2 = labios::fopen(output.c_str(), "w+");
  labios::fwrite_async(read_buf, sizeof(char), io_per_teration, fh2);
  labios::fclose(fh2);
#ifdef TIMERBASE
  pfs.pauseTime();
  free(read_buf);
  auto pfs_time = pfs.elapsed_time;
  double pfs_sum;
  MPI_Allreduce(&pfs_time, &pfs_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double pfs_mean = pfs_sum / comm_size;
  if (rank == 0)
    stream << "write_to_PFS," << pfs_mean << ",";
#endif
  global_timer.pauseTime();

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