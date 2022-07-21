cmake_minimum_required(VERSION 3.10)
project(labios VERSION 0.0.0 DESCRIPTION "Labios: A Label-based I/O system")

set(CMAKE_CXX_STANDARD 17)
include(GNUInstallDirs)
include_directories(include)
#define ones we have installed
set(MEMCACHED true)
set(NATS true)
set(MPI true)
#set(ROCKS true)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERMDM")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERTS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERTB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERNATS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERDM")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERW")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMER")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DFTIMER")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCOLLECT")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOVERHEADS")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTIMERBASE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTABIOS")

set(COMMON_SRC
        src/common/solver/default_solver.cpp
        src/common/config_manager.cpp src/common/solver/dp_solver.cpp
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
set(LABIOS_LIB_SRC ${COMMON_SRC}
        src/lib/driver.cpp
        src/lib/posix.cpp
        src/lib/mpi.cpp
        src/common/threadPool.cpp)
set(LABIOS_CLIENT_SRC  ${COMMON_SRC}
        src/client/main.cpp
        src/client/client.cpp
        src/common/threadPool.cpp)
set(LABIOS_SERVER_SRC  ${COMMON_SRC}
        src/system_manager/main.cpp
        src/system_manager/system_manager_service.cpp)
set(LABIOS_TS_SRC  ${COMMON_SRC}
        src/task_scheduler/main.cpp
        src/task_scheduler/task_scheduler.cpp
        src/task_scheduler/task_scheduler.h)
set(LABIOS_WM_SRC  ${COMMON_SRC}
        src/worker_manager/main.cpp
        src/worker_manager/worker_manager_service.cpp)
set(LABIOS_W_PROG_REPO
        src/worker/api/posix_client.cpp
        src/worker/program_repo/statistics.cpp)
set(LABIOS_WS_SRC  ${COMMON_SRC} ${LABIOS_W_PROG_REPO}
        src/worker/main.cpp
        src/worker/worker.cpp)

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


add_executable(labios_lib ${LABIOS_LIB_SRC})
add_executable(labios_client ${LABIOS_CLIENT_SRC})
add_executable(labios_server ${LABIOS_SERVER_SRC})
add_executable(labios_task_scheduler ${LABIOS_TS_SRC})
add_executable(labios_worker_manager ${LABIOS_WM_SRC})
add_executable(labios_worker ${LABIOS_WS_SRC})

if(DEFINED MPI)
    target_link_libraries(labios_lib ${MPI_LIBRARIES})
    target_link_libraries(labios_client ${MPI_LIBRARIES})
    target_link_libraries(labios_server ${MPI_LIBRARIES})
    target_link_libraries(labios_task_scheduler ${MPI_LIBRARIES})
    target_link_libraries(labios_worker_manager ${MPI_LIBRARIES})
    target_link_libraries(labios_worker ${MPI_LIBRARIES})
endif()
if(DEFINED ROCKS)
    target_link_libraries(labios_lib -lrocksdb)
    target_link_libraries(labios_client -lrocksdb)
    target_link_libraries(labios_server -lrocksdb)
    target_link_libraries(labios_task_scheduler -lrocksdb)
    target_link_libraries(labios_worker_manager -lrocksdb)
    target_link_libraries(labios_worker -lrocksdb)
endif()
if(DEFINED MEMCACHED)
    target_link_libraries(labios_lib -lmemcached)
    target_link_libraries(labios_client -lmemcached)
    target_link_libraries(labios_server -lmemcached)
    target_link_libraries(labios_task_scheduler -lmemcached)
    target_link_libraries(labios_worker_manager -lmemcached)
    target_link_libraries(labios_worker -lmemcached)
endif()
if(DEFINED NATS)
    target_link_libraries(labios_lib -lnats)
    target_link_libraries(labios_client -lnats)
    target_link_libraries(labios_server -lnats)
    target_link_libraries(labios_task_scheduler -lnats)
    target_link_libraries(labios_worker_manager -lnats)
    target_link_libraries(labios_worker -lnats)
endif()

target_link_libraries(labios_lib -pthread cityhash)
target_link_libraries(labios_client -pthread cityhash)
target_link_libraries(labios_server -pthread cityhash)
target_link_libraries(labios_task_scheduler -pthread cityhash)
target_link_libraries(labios_worker_manager -pthread cityhash)
target_link_libraries(labios_worker -pthread cityhash)

add_library(${PROJECT_NAME} SHARED ${LABIOS_LIB_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/labios.h)

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(${PROJECT_NAME} PRIVATE .)

target_link_libraries(labios ${CMAKE_CXX_LINK_FLAGS})

install(DIRECTORY include DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS labios_lib DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_client DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_server DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_task_scheduler DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_worker_manager DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS labios_worker DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})