#cmake_minimum_required(VERSION 3.25)
#project(labios_test)

#add_executable(labios_test labios_test.cc)
#target_link_libraries(labios_test labios::labios_client)

cmake_minimum_required(VERSION 3.25)
project(labios_test)

# Add your test executable
add_executable(labios_test labios_test.cc)
target_link_libraries(labios_test labios::labios_client)

# Build labios_wrapper as a shared library
add_library(labios_wrapper SHARED labios_wrapper.cpp)

# Link the same dependencies to the wrapper
target_link_libraries(labios_wrapper labios::labios_client)

# Optional: include the same headers
target_include_directories(labios_wrapper PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Optional: set output name explicitly
set_target_properties(labios_wrapper PROPERTIES OUTPUT_NAME "labios_wrapper")
