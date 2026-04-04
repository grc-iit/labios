cmake_minimum_required(VERSION 3.25)
project(labios VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/LabiosDependencies.cmake)

add_subdirectory(src/labios)
add_subdirectory(src/services)
add_subdirectory(tests)
