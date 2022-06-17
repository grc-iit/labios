cmake_minimum_required(VERSION 3.10)
project(labios VERSION 0.0.0 DESCRIPTION "Labios: A Label-based I/O system")

set(CMAKE_CXX_STANDARD 14)
include(GNUInstallDirs)
include_directories(external_libs/)
include_directories(include)
#define ones we have installed
set(MEMCACHED true)
set(NATS true)
set(MPI true)
#set(ROCKS true)
set(LIB_DIR /usr/local/lib)
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

set(COMMON_SRC "" include/labios/common/city.h src/common/city.cc
        src/common/solver/default_solver.cpp
        include/labios/common/solver/default_solver.h include/labios/common/config_manager.h src/common/config_manager.cpp src/common/solver/dp_solver.cpp include/labios/common/solver/dp_solver.h include/labios/common/solver/solver.h src/common/solver/random_solver.cpp include/labios/common/solver/random_solver.h src/common/solver/round_robin_solver.cpp include/labios/common/solver/round_robin_solver.h src/common/solver/knapsack.cpp external_libs/cereal/cereal.hpp include/labios/common/timer.h src/common/timer.cpp include/labios/common/enumerations.h include/labios/common/exceptions.h include/labios/common/data_structures.h src/common/data_manager/data_manager.cpp include/labios/common/data_manager/data_manager.h src/common/client_interface/distributed_hashmap.cpp include/labios/common/client_interface/distributed_hashmap.h include/labios/common/utilities.h src/common/metadata_manager/metadata_manager.cpp include/labios/common/metadata_manager/metadata_manager.h include/labios/common/constants.h src/labios_system.cpp include/labios/labios_system.h src/common/task_builder/task_builder.cpp include/labios/common/task_builder/task_builder.h src/common/client_interface/distributed_hashmap.cpp include/labios/common/client_interface/distributed_hashmap.h src/common/client_interface/distributed_queue.cpp include/labios/common/client_interface/distributed_queue.h src/common/external_clients/serialization_manager.cpp include/labios/common/external_clients/serialization_manager.h src/common/external_clients/rocksdb_impl.cpp include/labios/common/external_clients/rocksdb_impl.h src/common/external_clients/memcached_impl.cpp include/labios/common/external_clients/memcached_impl.h src/common/external_clients/nats_impl.cpp include/labios/common/external_clients/nats_impl.h)
set(LABIOS_LIB_SRC ${COMMON_SRC} src/lib/driver.cpp src/lib/posix.cpp include/labios/drivers/posix.h include/labios/drivers/mpi.h src/lib/mpi.cpp include/labios/common/return_codes.h src/common/threadPool.cpp include/labios/common/threadPool.h )
set(LABIOS_CLIENT_SRC  ${COMMON_SRC} "src/client/main.cpp" src/client/client.cpp src/client/client.h include/labios/common/exceptions.h include/labios.h include/labios/common/return_codes.h src/common/threadPool.cpp include/labios/common/threadPool.h)
set(LABIOS_SERVER_SRC  ${COMMON_SRC} "src/system_manager/main.cpp" src/system_manager/system_manager_service.cpp src/system_manager/system_manager_service.h include/labios/common/return_codes.h src/common/threadPool.cpp include/labios/common/threadPool.h)
set(LABIOS_TS_SRC  ${COMMON_SRC} src/task_scheduler/main.cpp src/task_scheduler/task_scheduler.cpp src/task_scheduler/task_scheduler.h include/labios/common/return_codes.h src/common/threadPool.cpp include/labios/common/threadPool.h)
set(LABIOS_WM_SRC  ${COMMON_SRC} "src/worker_manager/main.cpp" src/worker_manager/worker_manager_service.cpp src/worker_manager/worker_manager_service.h include/labios/common/return_codes.h src/common/threadPool.cpp include/labios/common/threadPool.h)
set(LABIOS_W_PROG_REPO src/worker/api/io_client.h src/worker/api/posix_client.cpp src/worker/api/posix_client.h src/worker/program_repo/statistics.h src/worker/program_repo/statistics.cpp include/labios/common/return_codes.h)
set(LABIOS_WS_SRC  ${COMMON_SRC} ${LABIOS_W_PROG_REPO} src/worker/main.cpp src/worker/worker.cpp src/worker/worker.h include/labios/common/return_codes.h src/common/threadPool.cpp include/labios/common/threadPool.h)


if(DEFINED MPI)
    find_package(MPI REQUIRED)
    set(CMAKE_CXX_COMPILE_FLAGS ${CMAKE_CXX_COMPILE_FLAGS} ${MPI_COMPILE_FLAGS})
    set(CMAKE_CXX_LINK_FLAGS ${CMAKE_CXX_LINK_FLAGS} ${MPI_LINK_FLAGS})
    include_directories(${MPI_INCLUDE_PATH})
endif()
if(DEFINED MEMCACHED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -L${LIB_DIR} -lmemcached -DMEMCACHED_P")
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
    target_link_libraries(labios_lib -L${LIB_DIR} -lmemcached)
    target_link_libraries(labios_client -L${LIB_DIR} -lmemcached)
    target_link_libraries(labios_server -L${LIB_DIR} -lmemcached)
    target_link_libraries(labios_task_scheduler -L${LIB_DIR} -lmemcached)
    target_link_libraries(labios_worker_manager -L${LIB_DIR} -lmemcached)
    target_link_libraries(labios_worker -L${LIB_DIR} -lmemcached)
endif()
if(DEFINED NATS)
    target_link_libraries(labios_lib -L${LIB_DIR} -lnats)
    target_link_libraries(labios_client -L${LIB_DIR} -lnats)
    target_link_libraries(labios_server -L${LIB_DIR} -lnats)
    target_link_libraries(labios_task_scheduler -L${LIB_DIR} -lnats)
    target_link_libraries(labios_worker_manager -L${LIB_DIR} -lnats)
    target_link_libraries(labios_worker -L${LIB_DIR} -lnats)
endif()

target_link_libraries(labios_lib -pthread)
target_link_libraries(labios_client -pthread)
target_link_libraries(labios_server -pthread)
target_link_libraries(labios_task_scheduler -pthread)
target_link_libraries(labios_worker_manager -pthread)
target_link_libraries(labios_worker -pthread)

add_library(${PROJECT_NAME} SHARED ${LABIOS_LIB_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER include/labios.h)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY external_libs/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

configure_file(labios.pc.in labios.pc @ONLY)

target_include_directories(${PROJECT_NAME} PRIVATE .)

target_link_libraries(labios ${CMAKE_CXX_LINK_FLAGS})

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (DEFINED ENV{PKG_CONFIG_PATH})
   install(FILES ${CMAKE_BINARY_DIR}/labios.pc
                  DESTINATION $ENV{PKG_CONFIG_PATH}/pkgconfig)
else()
   install(FILES ${CMAKE_BINARY_DIR}/basket.pc
                  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)
endif()