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

  CHIMAERA_CLIENT_INIT();
  chi::labios::Client client;

  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({1 * 1024 * 1024, 32});

  size_t current_offset = 0;
  hshm::Timer global_timer = hshm::Timer();
  char *write_buf[32];
  hipc::FullPtr<char> write_data[32];
  for (int i = 0; i < 32; ++i) {
    write_buf[i] = static_cast<char *>(malloc(1 * 1024 * 1024));
    gen_random(write_buf[i], 1 * 1024 * 1024);
    write_data[i] = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, 1 * 1024 * 1024);
    std::memcpy(write_data[i].ptr_, write_buf[i], 1 * 1024 * 1024);
  }
  global_timer.Resume();
#ifdef TIMERBASE
  Timer wbb = Timer();
  wbb.resumeTime();
#endif
 client.Create(
      HSHM_MCTX,
      chi::DomainQuery::GetDirectHash(chi::SubDomainId::kGlobalContainers, rank),
      chi::DomainQuery::GetGlobalBcast(), 
      "hacc_container_" + std::to_string(rank)
  );
#ifdef TIMERBASE
  wbb.pauseTime();
#endif
  global_timer.Pause();

  std::vector<std::pair<size_t, std::pair<std::string, chi::DomainQuery>>> operations =
      std::vector<std::pair<size_t, std::pair<std::string, chi::DomainQuery>>>();

  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        global_timer.Resume();
#ifdef TIMERBASE
        wbb.resumeTime();
#endif
        // Create unique key for each write operation
        std::string key = filename + "_iter_" + std::to_string(i) + "_chunk_" + std::to_string(j);
        
        // Get location for data placement
        auto query = chi::DomainQuery::GetDynamic();
        auto loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
        
        // Create metadata for the key
        client.MdGetOrCreate(HSHM_MCTX, query, key, current_offset, item[0], loc);
        
        // Perform write operation
        client.Write(HSHM_MCTX, loc, key, write_data[j].shm_, item[0]);

        operations.emplace_back(std::make_pair(
            item[0],
            std::make_pair(key, loc)));
#ifdef TIMERBASE
        wbb.pauseTime();
#endif
        global_timer.Pause();
        current_offset += item[0];
      }
    }
  }
  for (int i = 0; i < 32; ++i) {
    free(write_buf[i]);
  }
  global_timer.Resume();
#ifdef TIMERBASE
  wbb.resumeTime();
#endif
  // Verify writes by reading back data
  for (auto operation : operations) {
    size_t expected_size = operation.first;
    std::string key = operation.second.first;
    chi::DomainQuery loc = operation.second.second;
    
    // Allocate buffer for read verification
    hipc::FullPtr<char> read_verify_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, expected_size);
    
    // Read back the data for verification
    client.Read(HSHM_MCTX, loc, key, read_verify_data.shm_, expected_size);
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
  hipc::FullPtr<char> read_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, io_per_teration);
  global_timer.Resume();
#ifdef TIMERBASE
  Timer rbb = Timer();
  rbb.resumeTime();
#endif

#ifndef COLLECT
  std::string bulk_read_key = filename + "_bulk_read";
  auto query = chi::DomainQuery::GetDynamic();
  auto loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
  
  // Create metadata for bulk read
  client.MdGetOrCreate(HSHM_MCTX, query, bulk_read_key, 0, io_per_teration, loc);
  
  // Perform bulk read operation
  client.Read(HSHM_MCTX, loc, bulk_read_key, read_data.shm_, io_per_teration);
  
  // Copy read data to local buffer
  std::memcpy(read_buf, read_data.ptr_, io_per_teration);

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
   client.Create(
      HSHM_MCTX,
      chi::DomainQuery::GetDirectHash(chi::SubDomainId::kGlobalContainers, rank + 1000),
      chi::DomainQuery::GetGlobalBcast(), 
      "hacc_output_container_" + std::to_string(rank)
  );
  
  // Allocate buffer for final write
  hipc::FullPtr<char> final_write_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, io_per_teration);
  std::memcpy(final_write_data.ptr_, read_buf, io_per_teration);
  
  // Create final output key
  std::string final_key = output + "_final_output";
  auto final_query = chi::DomainQuery::GetDynamic();
  auto final_loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank + 1000, 0);
  
  // Create metadata and write final output
  client.MdGetOrCreate(HSHM_MCTX, final_query, final_key, 0, io_per_teration, final_loc);
  client.Write(HSHM_MCTX, final_loc, final_key, final_write_data.shm_, io_per_teration);

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
  global_timer.Pause();

  auto time = global_timer.GetSec();
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
  MPI_Finalize();
}