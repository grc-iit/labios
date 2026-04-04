include(FetchContent)

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
    Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
)

# cnats build options
set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_LIB_STATIC ON CACHE BOOL "" FORCE)
set(NATS_BUILD_LIB_SHARED OFF CACHE BOOL "" FORCE)

# hiredis build options
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)

# Build hiredis as a static library so Docker runtime images stay minimal.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Disable dependency test suites from polluting our CTest
set(_save_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(cnats hiredis tomlplusplus)
set(BUILD_TESTING ${_save_build_testing})

# Mark hiredis includes as SYSTEM to suppress -Wpedantic warnings from its C headers
get_target_property(_hiredis_inc hiredis INTERFACE_INCLUDE_DIRECTORIES)
if(_hiredis_inc)
    set_target_properties(hiredis PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_hiredis_inc}")
endif()

# Keep BUILD_SHARED_LIBS OFF for Catch2 too so the test binary is self-contained
FetchContent_MakeAvailable(Catch2)

# Restore for the rest of the project
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
