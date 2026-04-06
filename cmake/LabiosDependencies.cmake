include(FetchContent)

# SQLite3: prefer system install, fall back to source build.
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    FetchContent_Declare(
        sqlite3
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3450300.zip
    )
    FetchContent_MakeAvailable(sqlite3)
    add_library(sqlite3_lib STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_lib PUBLIC ${sqlite3_SOURCE_DIR})
    add_library(SQLite::SQLite3 ALIAS sqlite3_lib)
endif()

# Use source archives instead of git clones for Docker compatibility.
# Git clones require .git in the source tree for FetchContent update steps.
FetchContent_Declare(
    cnats
    URL https://github.com/nats-io/nats.c/archive/refs/tags/v3.9.1.tar.gz
)

FetchContent_Declare(
    hiredis
    URL https://github.com/redis/hiredis/archive/refs/tags/v1.2.0.tar.gz
)

FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
)

FetchContent_Declare(
    pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
)

FetchContent_Declare(
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
)

FetchContent_Declare(
    flatbuffers
    URL https://github.com/google/flatbuffers/archive/refs/tags/v24.3.25.tar.gz
)

# cnats build options
set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_LIB_STATIC ON CACHE BOOL "" FORCE)
set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)

# hiredis build options
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)

# FlatBuffers build options
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)

# Build hiredis as a static library so Docker runtime images stay minimal.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Disable dependency test suites from polluting our CTest
set(_save_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(cnats hiredis tomlplusplus flatbuffers)
set(BUILD_TESTING ${_save_build_testing})

# Mark hiredis includes as SYSTEM to suppress -Wpedantic warnings from its C headers
get_target_property(_hiredis_inc hiredis INTERFACE_INCLUDE_DIRECTORIES)
if(_hiredis_inc)
    set_target_properties(hiredis PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_hiredis_inc}")
endif()

# Python bindings (optional)
find_package(Python3 COMPONENTS Interpreter Development QUIET)
if(Python3_FOUND)
    FetchContent_MakeAvailable(pybind11)
    set(LABIOS_PYTHON_BINDINGS ON)
else()
    set(LABIOS_PYTHON_BINDINGS OFF)
    message(STATUS "Python3 not found, skipping Python bindings")
endif()

# Keep BUILD_SHARED_LIBS OFF for Catch2 too so the test binary is self-contained
FetchContent_MakeAvailable(Catch2)

# Restore for the rest of the project
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
