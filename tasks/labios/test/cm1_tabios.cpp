//
// Created by lukemartinlogan on 7/22/22.
//

#include <hermes_shm/util/timer.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <mpi.h>
#include "labios/labios_client.h"

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
    printf("USAGE: ./cm1_tabios [labios_conf] [file_path] [iteration]\n");
    exit(1);
  }

#ifdef COLLECT
  system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif

  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  stream << "cm1_tabios," << std::fixed << std::setprecision(10);
  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string filename = file_path + "/test_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;

  printf("HERE0\n");

  CHIMAERA_CLIENT_INIT();
  chi::labios::Client client;

  hshm::Timer global_timer = hshm::Timer();
  global_timer.Resume();

  client.Create(
        HSHM_MCTX,
        chi::DomainQuery::GetDirectHash(chi::SubDomainId::kGlobalContainers, rank),
        chi::DomainQuery::GetGlobalBcast(), 
        "labios_container_" + std::to_string(rank)
    );

  global_timer.Pause();

  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({1 * 1024 * 1024, 32});


  size_t current_offset = 0;
  
  // FILE *fh = fopen(filename.c_str(), "w+");
  
  char *write_buf[32];
  hipc::FullPtr<char> write_data[32];

  for (int i = 0; i < 32; ++i) {
    write_buf[i] = static_cast<char *>(malloc(1 * 1024 * 1024));
    gen_random(write_buf[i], 1 * 1024 * 1024);
    write_data[i] = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, 1 * 1024 * 1024);
    std::memcpy(write_data[i].ptr_, write_buf[i], 1 * 1024 * 1024);
  }
  std::vector<std::pair<size_t, std::pair<std::string, chi::DomainQuery>>> operations =
        std::vector<std::pair<size_t, std::pair<std::string, chi::DomainQuery>>>();

  printf("HERE1\n");
  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        global_timer.Resume();
        std::string key = filename + "_iter_" + std::to_string(i) + "_chunk_" + std::to_string(j);
        auto query = chi::DomainQuery::GetDynamic();
        auto loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
        client.MdGetOrCreate(HSHM_MCTX, query, key, current_offset, item[0], loc);
        client.Write(HSHM_MCTX, loc, key, write_data[j].shm_, item[0]);
        operations.emplace_back(std::make_pair(item[0], std::make_pair(key, loc)));

        global_timer.Pause();
        current_offset += item[0];
      }
    }
  }
  printf("HERE2\n");
  global_timer.Resume();
  for (auto operation : operations) {
    size_t expected_size = operation.first;
    std::string key = operation.second.first;
    chi::DomainQuery loc = operation.second.second;
    hipc::FullPtr<char> read_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, expected_size);
    client.Read(HSHM_MCTX, loc, key, read_data.shm_, expected_size);
    printf("HERE2.5: %zu\n", expected_size);

    // auto bytes = fwrite_wait(operation.second);
    // printf("HERE2.5: %ul\n", bytes);
    // if (bytes != operation.first)
    //   std::cerr << "Write failed\n";
  }
  global_timer.Pause();
  for (int i = 0; i < 32; ++i) {
    free(write_buf[i]);
  }
  global_timer.Resume();
  if (rank == 0)
    std::cerr << "Write finished\n";
  // fclose(fh);
  global_timer.Pause();
  printf("HERE3");

  auto time = global_timer.GetSec();
  double sum, max, min;
  MPI_Allreduce(&time, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
  double mean = sum / comm_size;
  if (rank == 0) {
#ifdef COLLECT
    double ts = get_average_ts();
    double worker = get_average_worker();
    stream << ts << "," << worker << ",";
#endif
    stream << mean << "," << max << "," << min << "\n";
    std::cerr << stream.str();
  }
  MPI_Finalize();
}