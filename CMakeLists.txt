cmake_minimum_required(VERSION 3.25)
project(labios VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All targets need -fPIC so the static lib can link into the LD_PRELOAD .so
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/LabiosDependencies.cmake)

enable_testing()

add_subdirectory(src/labios)
add_subdirectory(src/services)

# POSIX intercept shared library (LD_PRELOAD)
add_library(labios_intercept SHARED src/drivers/posix_intercept.cpp)
target_link_libraries(labios_intercept PRIVATE labios ${CMAKE_DL_LIBS})
set_target_properties(labios_intercept PROPERTIES
    OUTPUT_NAME "labios_intercept"
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Python bindings (optional, requires Python3 development headers)
if(LABIOS_PYTHON_BINDINGS)
    pybind11_add_module(_labios src/python/labios_module.cpp)
    target_link_libraries(_labios PRIVATE labios)
    set_target_properties(_labios PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    )
endif()

add_subdirectory(tests)
