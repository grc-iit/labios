set(LABIOS_FBS_SCHEMA ${PROJECT_SOURCE_DIR}/schemas/label.fbs)
set(LABIOS_FBS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(LABIOS_FBS_GEN_HEADER ${LABIOS_FBS_GEN_DIR}/label_generated.h)

file(MAKE_DIRECTORY ${LABIOS_FBS_GEN_DIR})

add_custom_command(
    OUTPUT ${LABIOS_FBS_GEN_HEADER}
    COMMAND flatc --cpp -o ${LABIOS_FBS_GEN_DIR} ${LABIOS_FBS_SCHEMA}
    DEPENDS ${LABIOS_FBS_SCHEMA} flatc
    COMMENT "Generating FlatBuffers header from label.fbs"
)

add_custom_target(labios_fbs_generated DEPENDS ${LABIOS_FBS_GEN_HEADER})

add_library(labios STATIC
    catalog_manager.cpp
    client.cpp
    config.cpp
    label.cpp
    label_manager.cpp
    content_manager.cpp
    session.cpp
    shuffler.cpp
    solver/random.cpp
    solver/round_robin.cpp
    transport/nats.cpp
    transport/redis.cpp
    adapter/fd_table.cpp
    adapter/posix_adapter.cpp
)

add_dependencies(labios labios_fbs_generated)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
    SYSTEM PUBLIC
        ${LABIOS_FBS_GEN_DIR}
)

target_link_libraries(labios
    PUBLIC
        nats_static
        hiredis
        tomlplusplus::tomlplusplus
        flatbuffers
        ${CMAKE_DL_LIBS}
)

target_compile_features(labios PUBLIC cxx_std_20)
