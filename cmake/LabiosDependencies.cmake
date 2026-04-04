include(FetchContent)

FetchContent_Declare(
    cnats
    GIT_REPOSITORY https://github.com/nats-io/nats.c.git
    GIT_TAG        v3.9.1
)

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.2.0
    SYSTEM
)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
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
# Use CACHE FORCE because hiredis option(BUILD_SHARED_LIBS) clears normal vars.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Disable dependency test suites from polluting our CTest
set(_save_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(cnats hiredis tomlplusplus)
set(BUILD_TESTING ${_save_build_testing})

# Restore BUILD_SHARED_LIBS for the rest of the project
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2)
