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
  std::cout << "Kmeans started by Rajni" << std::endl;
  MPI_Init(&argc, &argv);
  if (argc != 5) {
    printf(
        "USAGE: ./kmean_tabios [labios_conf] [file_path] [iter] [pfs_path]\n");
    exit(1);
  }

#ifdef COLLECT
  system("sh /home/cc/nfs/aetrio/scripts/log_reset.sh");
#endif
  std::stringstream stream;
  int rank, comm_size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
  stream << "average_kmeans_tabios," << std::fixed << std::setprecision(10);

    // Initialize Chimaera client
  CHIMAERA_CLIENT_INIT();
  
  // Create Labios client
  chi::labios::Client client;

  std::string file_path = argv[2];
  int iteration = atoi(argv[3]);
  std::string pfs_path = argv[4];
  std::string filename = file_path + "test_" + std::to_string(rank) + ".dat";
  size_t io_per_teration = 32 * 1024 * 1024;
  std::vector<std::array<size_t, 2>> workload =
      std::vector<std::array<size_t, 2>>();
  workload.push_back({32 * 1024, 1024});

  size_t current_offset = 0;
  hshm::Timer global_timer = hshm::Timer();
  char *write_buf = new char[io_per_teration];
  gen_random(write_buf, io_per_teration);

  hipc::FullPtr<char> initial_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, io_per_teration);
  std::memcpy(initial_data.ptr_, write_buf, io_per_teration);
  global_timer.Resume();
#ifdef TIMERBASE
  Timer map = Timer();
  map.resumeTime();
#endif
   client.Create(
      HSHM_MCTX,
      chi::DomainQuery::GetDirectHash(chi::SubDomainId::kGlobalContainers, rank),
      chi::DomainQuery::GetGlobalBcast(), 
      "kmeans_container_" + std::to_string(rank)
  );
#ifdef TIMERBASE
  map.pauseTime();
#endif
  global_timer.Pause();

// Write initial dataset to container
  std::string dataset_key = filename + "_dataset";
  auto query = chi::DomainQuery::GetDynamic();
  auto loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
  
  client.MdGetOrCreate(HSHM_MCTX, query, dataset_key, 0, io_per_teration, loc);
  client.Write(HSHM_MCTX, loc, dataset_key, initial_data.shm_, io_per_teration);
  
  delete (write_buf);
  size_t count = 0;
  std::vector<std::pair<size_t, std::pair<std::string, chi::DomainQuery>>> operations =
      std::vector<std::pair<size_t, std::pair<std::string, chi::DomainQuery>>>();

  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0)
    std::cerr << "Data created Done\n";
  for (int i = 0; i < iteration; ++i) {
    for (auto item : workload) {
      for (int j = 0; j < item[1]; ++j) {
        if (count % 32 == 0) {
          std::random_device rd;
          std::mt19937 generator(rd());
          std::uniform_int_distribution<int> dist(0, 31);
          auto rand_offset = (dist(generator) * 1 * 1024 * 1024);
          global_timer.Resume();
#ifdef TIMERBASE
          map.resumeTime();
#endif

#ifdef TIMERBASE
          map.pauseTime();
#endif
          global_timer.Pause();
          current_offset = static_cast<size_t>(rand_offset);
        }
        global_timer.Resume();
#ifdef TIMERBASE
        map.resumeTime();
#endif
#ifndef COLLECT
        // Create unique key for each read operation
        std::string read_key = dataset_key + "_read_" + std::to_string(i) + "_" + std::to_string(j) + "_offset_" + std::to_string(current_offset);
        
        // For K-means, we simulate reading different chunks of the dataset
        // Create metadata for this specific chunk
        auto read_query = chi::DomainQuery::GetDynamic();
        auto read_loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
        
        client.MdGetOrCreate(HSHM_MCTX, read_query, read_key, current_offset, item[0], read_loc);
        
        // Store operation info for later execution
        operations.emplace_back(std::make_pair(
            item[0], std::make_pair(read_key, read_loc)));

#endif
#ifdef TIMERBASE
        map.pauseTime();
#endif
        global_timer.Pause();
        current_offset += item[0];
        count++;
      }
    }
  }
  global_timer.Resume();
#ifdef TIMERBASE
  map.resumeTime();
#endif
  for (auto operation : operations) {
    size_t read_size = operation.first;
      std::string read_key = operation.second.first;
      chi::DomainQuery read_loc = operation.second.second;
      
      // Allocate buffer for this read operation
      hipc::FullPtr<char> read_buffer = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, read_size);
      
      // Perform the read operation
      client.Read(HSHM_MCTX, read_loc, read_key, read_buffer.shm_, read_size);
    }

  MPI_Barrier(MPI_COMM_WORLD);
  if (rank == 0)
    std::cerr << "Read Done\n";

  if (rank == 0)
    std::cerr << "Starting final output phase...\n";
    
#ifdef TIMERBASE
  map.pauseTime();
#endif
  global_timer.Pause();
#ifdef TIMERBASE
  auto map_time = map.elapsed_time;
  double map_sum;
  MPI_Allreduce(&map_time, &map_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double map_mean = map_sum / comm_size;
  if (rank == 0)
    stream << map_mean << ",";
#endif
  std::string finalname = pfs_path + "final_" + std::to_string(rank) + ".dat";

  global_timer.Resume();
#ifdef TIMERBASE
  Timer reduce = Timer();
  reduce.resumeTime();
#endif

  // Use the SAME container instead of creating a new one
  if (rank == 0)
    std::cerr << "Creating final output (using same container)...\n";

#ifdef TIMERBASE
  reduce.pauseTime();
#endif
  global_timer.Pause();

  char out_buff[1024 * 1024];
  gen_random(out_buff, 1024 * 1024);
  hipc::FullPtr<char> output_data = CHI_CLIENT->AllocateBuffer(HSHM_MCTX, 1024 * 1024);
  std::memcpy(output_data.ptr_, out_buff, 1024 * 1024);

  global_timer.Resume();
#ifdef TIMERBASE
  reduce.resumeTime();
#endif

  if (rank == 0)
    std::cerr << "Writing final output...\n";

  // Use the same container and location as the original data
  std::string final_key = "kmeans_final_output_" + std::to_string(rank);
  auto final_query = chi::DomainQuery::GetDynamic();
  // USE SAME LOCATION as original data (rank)
  auto final_loc = chi::DomainQuery::GetDirectId(chi::SubDomain::kGlobalContainers, rank, 0);
  
  client.MdGetOrCreate(HSHM_MCTX, final_query, final_key, 0, 1024 * 1024, final_loc);
  
  if (rank == 0)
    std::cerr << "Metadata created, performing write...\n";
    
  client.Write(HSHM_MCTX, final_loc, final_key, output_data.shm_, 1024 * 1024);

  if (rank == 0)
    std::cerr << "Final write completed!\n";

#ifdef TIMERBASE
  reduce.pauseTime();
#endif
  global_timer.Pause();
#ifdef TIMERBASE
  auto red_time = reduce.elapsed_time;
  double red_sum, max, min;
  MPI_Allreduce(&red_time, &red_sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&red_time, &max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&red_time, &min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
  double red_mean = red_sum / comm_size;
  if (rank == 0)
    stream << red_mean << "," << max << "," << min;
#endif

  if (rank == 0)
    std::cerr << "Computing final timing results...\n";

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
    std::cerr << "K-means test completed successfully!\n";

  MPI_Finalize();
  return 0;
}