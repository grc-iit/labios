cmake_minimum_required(VERSION 3.10)
project(
  labios
  VERSION 0.0.0
  DESCRIPTION "Labios: A Label-based I/O system")

set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)
include_directories(include)
include_directories(${INCLUDE_PATH})
link_libraries(${LIBRARY_PATH})
# define ones we have installed
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(MEMCACHED true)
set(NATS true)
set(MPI true)
# set(ROCKS true) set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERMDM")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERTS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERTB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERNATS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERDM")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERW")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMER") set(CMAKE_CXX_FLAGS
# "${CMAKE_CXX_FLAGS} -DFTIMER") set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}
# -DCOLLECT")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOVERHEADS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERBASE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTABIOS")

set(COMMON_SRC
    src/common/solver/default_solver.cpp
    src/common/config_manager.cpp
    src/common/solver/dp_solver.cpp
    src/common/solver/random_solver.cpp
    src/common/solver/round_robin_solver.cpp
    src/common/solver/knapsack.cpp
    src/common/timer.cpp
    src/common/data_manager/data_manager.cpp
    src/common/client_interface/distributed_hashmap.cpp
    src/common/metadata_manager/metadata_manager.cpp
    src/labios_system.cpp
    src/common/client_interface/distributed_hashmap.cpp
    src/common/client_interface/distributed_queue.cpp
    src/common/external_clients/serialization_manager.cpp
    src/common/external_clients/rocksdb_impl.cpp
    src/common/external_clients/memcached_impl.cpp
    src/common/external_clients/nats_impl.cpp
    src/common/task_builder/task_builder.cpp)
set(LABIOS_LIB_SRC src/lib/posix.cpp src/lib/mpi.cpp src/common/threadPool.cpp)
set(LABIOS_CLIENT_SRC src/client/main.cpp src/client/client.cpp
                      src/common/threadPool.cpp)
set(LABIOS_SERVER_SRC src/system_manager/main.cpp
                      src/system_manager/system_manager_service.cpp)
set(LABIOS_TS_SRC
    src/task_scheduler/main.cpp src/task_scheduler/task_scheduler.cpp
    src/task_scheduler/task_scheduler.h)
set(LABIOS_WM_SRC src/worker_manager/main.cpp
                  src/worker_manager/worker_manager_service.cpp)
set(LABIOS_WS_SRC
    src/worker/api/posix_client.cpp src/worker/program_repo/statistics.cpp
    src/worker/main.cpp src/worker/worker.cpp)

if(DEFINED MPI)
  find_package(MPI REQUIRED)
  set(CMAKE_CXX_COMPILE_FLAGS ${CMAKE_CXX_COMPILE_FLAGS} ${MPI_COMPILE_FLAGS})
  set(CMAKE_CXX_LINK_FLAGS ${CMAKE_CXX_LINK_FLAGS} ${MPI_LINK_FLAGS})
  include_directories(${MPI_INCLUDE_PATH})
endif()
if(DEFINED MEMCACHED)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lmemcached -DMEMCACHED_P")
endif()
if(DEFINED ROCKS)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DROCKS_P")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")

add_library(labios_common SHARED ${COMMON_SRC})
add_library(${PROJECT_NAME} SHARED ${LABIOS_LIB_SRC})
add_executable(labios_client ${LABIOS_CLIENT_SRC})
add_executable(labios_server ${LABIOS_SERVER_SRC})
add_executable(labios_task_scheduler ${LABIOS_TS_SRC})
add_executable(labios_worker_manager ${LABIOS_WM_SRC})
add_executable(labios_worker ${LABIOS_WS_SRC})

function(labios_set_libs target)
  target_link_libraries(${target} yaml-cpp)
  if(DEFINED MPI)
    target_link_libraries(${target} ${MPI_LIBRARIES})
  endif()
  if(DEFINED ROCKS)
    target_link_libraries(${target} -lrocksdb)
  endif()
  if(DEFINED MEMCACHED)
    target_link_libraries(${target} -lmemcached)
  endif()
  if(DEFINED NATS)
    target_link_libraries(${target} -lnats)
  endif()
  target_link_libraries(${target} -pthread cityhash)
  if(NOT ${target} STREQUAL "labios_common")
    message(${target})
    add_dependencies(${target} labios_common)
    target_link_libraries(${target} labios_common)
  endif()
endfunction()

labios_set_libs(labios_common)
labios_set_libs(labios_client)
labios_set_libs(labios_server)
labios_set_libs(labios_task_scheduler)
labios_set_libs(labios_worker_manager)
labios_set_libs(labios_worker)
labios_set_libs(${PROJECT_NAME})

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES VERSION ${PROJECT_VERSION}
             SOVERSION 1
             PUBLIC_HEADER include/labios.h)

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${PROJECT_NAME} PRIVATE .)

target_link_libraries(labios ${CMAKE_CXX_LINK_FLAGS})

install(DIRECTORY include DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS labios_common DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS labios_client DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_server DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_task_scheduler DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_worker_manager DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_worker DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
  TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

add_subdirectory(benchmark)
