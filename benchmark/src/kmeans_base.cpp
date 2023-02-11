//
// Created by lukemartinlogan on 7/22/22.
//

#include "util.h"

int main(int argc, char **argv) {
  labios::MPI_Init(&argc, &argv);
  if (argc != 5) {
    printf("USAGE: ./kmean_base [labios_conf] [file_path] [iter] [pfs_path]\n");
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
  Timer global_timer = Timer();

  global_timer.resumeTime();
  FILE *fh = std::fopen(filename.c_str(), "w+");
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
#ifdef TIMERBASE
  Timer map = Timer();
  map.resumeTime();
#endif
  int fd = open(filename.c_str(), O_DIRECT | O_RDWR | O_TRUNC, mode);
#ifdef TIMERBASE
  map.pauseTime();
#endif
  global_timer.pauseTime();

  char *write_buf = new char[io_per_teration];
  gen_random(write_buf, io_per_teration);
  std::fwrite(write_buf, sizeof(char), io_per_teration, fh);
  std::fflush(fh);
  //    write(fd,write_buf,io_per_teration);
  //    fsync(fd);
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
          global_timer.resumeTime();
#ifdef TIMERBASE
          map.resumeTime();
#endif
          std::fseek(fh, rand_offset, SEEK_SET);
          // lseek(fd,rand_offset,SEEK_SET);
          MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
          map.pauseTime();
#endif
          global_timer.pauseTime();
        }
        void *read_buf;
        read_buf = memalign(align * 2, item[0] + align);
        if (read_buf == NULL)
          std::cerr << "memalign\n";
        read_buf += align;
        global_timer.resumeTime();
#ifdef TIMERBASE
        map.resumeTime();
#endif
        // auto bytes = std::fread(read_buf,sizeof(char),item[0],fh);

        auto bytes = read(fd, read_buf, item[0]);
        if (bytes != item[0])
          std::cerr << "Read failed:" << bytes << "\n";
        MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
        map.pauseTime();
#endif
        global_timer.pauseTime();
        current_offset += item[0];
        count++;
      }
    }
  }
  global_timer.resumeTime();
#ifdef TIMERBASE
  map.resumeTime();
#endif
  // std::fclose(fh);
  close(fd);
  MPI_Barrier(MPI_COMM_WORLD);
#ifdef TIMERBASE
  map.pauseTime();
#endif
  global_timer.pauseTime();
#ifdef TIMERBASE
  if (rank == 0)
    stream << map.elapsed_time << ",";
#endif
#ifdef TIMERBASE
  Timer reduce = Timer();
#endif
  MPI_File outFile;
  MPI_Info info;
  MPI_Info_create(&info);
  MPI_Info_set(info, "direct_write", "true");
  std::string final = pfs_path + "final.dat";
  global_timer.resumeTime();
#ifdef TIMERBASE
  reduce.resumeTime();
#endif
  MPI_File_open(MPI_COMM_WORLD, final.c_str(), MPI_MODE_CREATE | MPI_MODE_RDWR,
                info, &outFile);
  MPI_File_set_view(outFile, static_cast<MPI_Offset>(rank * 1024 * 1024),
                    MPI_CHAR, MPI_CHAR, "native", MPI_INFO_NULL);
#ifdef TIMERBASE
  reduce.pauseTime();
#endif
  global_timer.pauseTime();
  char out_buff[1024 * 1024];
  gen_random(out_buff, 1024 * 1024);
  global_timer.resumeTime();
#ifdef TIMERBASE
  reduce.resumeTime();
#endif
  MPI_File_write(outFile, out_buff, (1024 * 1024), MPI_CHAR, MPI_STATUS_IGNORE);
  MPI_File_close(&outFile);
#ifdef TIMERBASE
  reduce.pauseTime();
#endif
  global_timer.pauseTime();
#ifdef TIMERBASE
  if (rank == 0)
    stream << reduce.elapsed_time << ",";
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