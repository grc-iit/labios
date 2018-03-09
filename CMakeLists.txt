cmake_minimum_required(VERSION 3.8)
project(porus_main)

set(CMAKE_CXX_STANDARD 11)
include_directories(libs/)
set(COMMON_SRC "" libs/cereal/cereal.hpp src/common/enumeration.h src/common/exception.h src/common/structure.h src/common/data_manager/data_manager.cpp src/common/data_manager/data_manager.h src/common/external_clients/DistributedHashMap.cpp src/common/external_clients/DistributedHashMap.h src/common/utility.h src/common/metadata_manager/metadata_manager.cpp src/common/metadata_manager/metadata_manager.h src/common/constants.h src/System.cpp src/System.h src/common/task_handler/task_handler.cpp src/common/task_handler/task_handler.h src/common/external_clients/DistributedHashMap.cpp src/common/external_clients/DistributedHashMap.h src/common/external_clients/DistributedQueue.cpp src/common/external_clients/DistributedQueue.h src/common/external_clients/serialization_manager.cpp src/common/external_clients/serialization_manager.h src/common/external_clients/RocksDBImpl.cpp src/common/external_clients/RocksDBImpl.h src/common/external_clients/MemcacheDImpl.cpp src/common/external_clients/MemcacheDImpl.h)
set(PORUS_LIB_SRC ${COMMON_SRC} src/lib/driver.cpp src/lib/posix.cpp include/posix.h)
set(PORUS_CLIENT_SRC  ${COMMON_SRC} "src/client/main.cpp" src/client/PorusClient.cpp src/client/PorusClient.h src/lib/driver.cpp src/common/exception.h)
set(PORUS_SERVER_SRC  ${COMMON_SRC} "src/server/main.cpp")
set(PORUS_TS_SRC  ${COMMON_SRC} "src/task_scheduler/main.cpp")
set(PORUS_WM_SRC  ${COMMON_SRC} "src/worker_manager/main.cpp")
set(PORUS_WS_SRC  ${COMMON_SRC} "src/worker/main.cpp")
set(PORUS_W_PROG_STATS_SRC  ${COMMON_SRC} "src/worker/program_repo/statistics.h src/worker/program_repo/statistics.cpp")

find_package(MPI REQUIRED)
set(CMAKE_CXX_COMPILE_FLAGS ${CMAKE_CXX_COMPILE_FLAGS} ${MPI_COMPILE_FLAGS})
set(CMAKE_CXX_LINK_FLAGS ${CMAKE_CXX_LINK_FLAGS} ${MPI_LINK_FLAGS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -L/usr/local/lib/ -lmemcached -w")

include_directories(${MPI_INCLUDE_PATH})
include_directories(/home/hariharan/Downloads/rocksdb/include)
add_executable(porus_lib ${PORUS_LIB_SRC})
add_executable(porus_client ${PORUS_CLIENT_SRC})
add_executable(porus_server ${PORUS_SERVER_SRC})
add_executable(porus_task_scheduler ${PORUS_TS_SRC})
add_executable(porus_worker_manager ${PORUS_WM_SRC})
add_executable(porus_worker ${PORUS_WS_SRC})


target_link_libraries(porus_lib ${MPI_LIBRARIES} -pthread -lrocksdb -lnats memcached)
target_link_libraries(porus_client ${MPI_LIBRARIES} -pthread -lrocksdb -lnats memcached)
target_link_libraries(porus_server ${MPI_LIBRARIES} -pthread -lrocksdb -lnats memcached)
target_link_libraries(porus_task_scheduler ${MPI_LIBRARIES} -pthread -lrocksdb  -lnats memcached)
target_link_libraries(porus_worker_manager ${MPI_LIBRARIES} -pthread -lrocksdb -lnats memcached)
target_link_libraries(porus_worker ${MPI_LIBRARIES} -pthread -lrocksdb -lnats memcached)

#worker programs
add_executable(porus_worker_stats ${PORUS_W_PROG_STATS_SRC})
target_link_libraries(porus_worker_stats ${MPI_LIBRARIES} -pthread)