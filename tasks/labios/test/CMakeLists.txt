cmake_minimum_required(VERSION 3.25)
project(labios_test)

#find_package(Chimaera CONFIG REQUIRED)
#find_package(Labios CONFIG REQUIRED)
#add_subdirectory(tasks)

add_executable(labios_test labios_test.cc)
target_link_libraries(labios_test labios::labios_client)