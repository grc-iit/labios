add_executable(labios-cpp-api-example native/cpp_golden.cpp)
target_link_libraries(labios-cpp-api-example PRIVATE labios)

add_executable(labios-c-api-example native/c_golden.c)
target_link_libraries(labios-c-api-example PRIVATE labios)
set_target_properties(labios-c-api-example PROPERTIES LINKER_LANGUAGE CXX)
