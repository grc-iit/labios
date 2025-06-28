#cmake_minimum_required(VERSION 3.25)
#project(labios_test)

#add_executable(labios_test labios_test.cc)
#target_link_libraries(labios_test labios::labios_client)

cmake_minimum_required(VERSION 3.25)
project(labios_test)

# Add your test executable
#add_executable(labios_test labios_test.cc)
#target_link_libraries(labios_test labios::labios_client)

find_package(MPI REQUIRED)
add_executable(mpi_labios_test labios_test.cc)
target_link_libraries(mpi_labios_test PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(cm1_base cm1_base.cpp)
target_link_libraries(cm1_base PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(cm1_tabios cm1_tabios.cpp)
target_link_libraries(cm1_tabios PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(hacc_base hacc_base.cpp)
target_link_libraries(hacc_base PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(hacc_tabios hacc_tabios.cpp)
target_link_libraries(hacc_tabios PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(kmeans_base kmeans_base.cpp)
target_link_libraries(kmeans_base PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(kmeans_tabios kmeans_tabios.cpp)
target_link_libraries(kmeans_tabios PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(montage_base montage_base.cpp)
target_link_libraries(montage_base PRIVATE MPI::MPI_CXX labios::labios_client)

add_executable(montage_tabios montage_tabios.cpp)
target_link_libraries(montage_tabios PRIVATE MPI::MPI_CXX labios::labios_client)

# Build labios_wrapper as a shared library
add_library(labios_wrapper SHARED labios_wrapper.cpp)

# Link the same dependencies to the wrapper
target_link_libraries(labios_wrapper labios::labios_client)

# Optional: include the same headers

target_include_directories(labios_wrapper PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Optional: set output name explicitly
set_target_properties(labios_wrapper PROPERTIES OUTPUT_NAME "labios_wrapper")
install(TARGETS mpi_labios_test
    cm1_base cm1_tabios
    hacc_base hacc_tabios
    kmeans_base kmeans_tabios
    montage_base montage_tabios
    labios_wrapper
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)