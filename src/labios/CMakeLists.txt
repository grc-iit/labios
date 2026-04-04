add_library(labios STATIC
    config.cpp
    transport/nats.cpp
    transport/redis.cpp
)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(labios
    PUBLIC
        nats_static
        hiredis
        tomlplusplus::tomlplusplus
)

target_compile_features(labios PUBLIC cxx_std_20)
