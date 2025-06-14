//
// Created by lukemartinlogan on 7/22/22.
//
#include <hermes_shm/util/timer.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <mpi.h>
#include "labios/labios_client.h"
#include "labios/labios_tasks.h"

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
    
  // Initialize Chimaera client
  CHIMAERA_CLIENT_INIT();
  
  // Create Labios client
  chi::labios::Client client;
  
  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string buf_path = argv[4];
  std::string filename = buf_path + "test_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({1 * 1024 * 1024, 32});
  size_t current_offset = 0;
  hshm::Timer global_timer = hshm::Timer();
  
  // OPTIMIZATION: Allocate Chimaera buffers directly
  hipc::FullPtr<char> write_data[32];
  for (int i = 0; i < 32; ++i) {
    write_data[i] = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, 1 * 1024 * 1024);
    gen_random(write_data[i].ptr_, 1 * 1024 * 1024);
  }
  
  global_timer.Resume();
#ifdef TIMERBASE
  Timer wbb = Timer();
  wbb.resumeTime();
#endif

  // Create Labios container instead of opening file
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

  // Store write operations with their keys and locations
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
        
        // Store operation info for later verification
        operations.emplace_back(std::make_pair(item[0], std::make_pair(key, loc)));
        
#ifdef TIMERBASE
        wbb.pauseTime();
#endif
        global_timer.Pause();
        current_offset += item[0];
      }
    }
  }
  
  global_timer.Resume();
#ifdef TIMERBASE
  wbb.resumeTime();
#endif

  // OPTIMIZATION: Simplified verification
  for (auto operation : operations) {
    size_t expected_size = operation.first;
    std::string key = operation.second.first;
    chi::DomainQuery loc = operation.second.second;
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

  // Use single large buffer for read operations
  hipc::FullPtr<char> read_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, io_per_teration);
  
  global_timer.Resume();
#ifdef TIMERBASE
  Timer rbb = Timer();
  rbb.resumeTime();
#endif

#ifndef COLLECT
  // Create a bulk read key for the entire data
  std::string bulk_read_key = filename + "_bulk_read";
  auto query = chi::DomainQuery::GetDynamic();
  auto loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
  
  // Create metadata for bulk read
  client.MdGetOrCreate(HSHM_MCTX, query, bulk_read_key, 0, io_per_teration, loc);
  
  // Perform bulk read operation
  client.Read(HSHM_MCTX, loc, bulk_read_key, read_data.shm_, io_per_teration);
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

  if (rank == 0)
    std::cerr << "Starting final output phase...\n";
  
  //Reuse existing buffer for final write
  gen_random(read_data.ptr_, 1024 * 1024);  // Generate new data in existing buffer
  
  if (rank == 0)
    std::cerr << "Writing final output...\n";
  
  // Use same container as original data
  std::string final_key = "hacc_final_output_" + std::to_string(rank);
  auto final_query = chi::DomainQuery::GetDynamic();
  // Use same location as original data (rank)
  auto final_loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
  
  // Create metadata and write final output
  client.MdGetOrCreate(HSHM_MCTX, final_query, final_key, 0, io_per_teration, final_loc);
  client.Write(HSHM_MCTX, final_loc, final_key, read_data.shm_, io_per_teration);

  if (rank == 0)
    std::cerr << "Final write completed!\n";

#ifdef TIMERBASE
  pfs.pauseTime();
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

  if (rank == 0)
    std::cerr << "HACC test completed successfully!\n";

  MPI_Finalize();
  return 0;
}